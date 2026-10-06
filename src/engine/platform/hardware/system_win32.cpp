#if defined(_WIN32)

#include "engine/platform/hardware/system_info.hpp"
#include "engine/platform/hardware/system_internal.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <process.h>

#include <mutex>
#include <string>
#include <vector>

namespace Shard::Engine::Core::Platform {

    namespace Internal {

        void QueryOsInfo(OsInfo& out)
        {
            out.name = "Windows";

            // GetVersionEx lies to applications without a manifest : ask ntdll.
            using RtlGetVersionFn = LONG (WINAPI*)(OSVERSIONINFOW*);
            OSVERSIONINFOW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
                if (auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion"))))
                    fn(&info);
            out.version = std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion) + "." + std::to_string(info.dwBuildNumber);

            SYSTEM_INFO si{};
            GetNativeSystemInfo(&si);
            switch (si.wProcessorArchitecture)
            {
                case PROCESSOR_ARCHITECTURE_AMD64: out.arch = "x86_64"; break;
                case PROCESSOR_ARCHITECTURE_ARM64: out.arch = "arm64"; break;
                case PROCESSOR_ARCHITECTURE_INTEL: out.arch = "x86"; break;
                default: out.arch = "unknown"; break;
            }
        }

        void QueryCoreCounts(CpuInfo& out)
        {
            const DWORD logical = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
            if (logical > 0)
                out.logicalCores = logical;

            DWORD length = 0;
            GetLogicalProcessorInformation(nullptr, &length);
            if (length == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                return;

            std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
            if (!GetLogicalProcessorInformation(buffer.data(), &length))
                return;

            uint32_t cores = 0;
            for (const auto& entry : buffer)
                if (entry.Relationship == RelationProcessorCore)
                    ++cores;
            if (cores > 0)
                out.physicalCores = cores;
        }
    }

    MemoryInfo QueryMemory()
    {
        MemoryInfo info;
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status))
        {
            info.totalPhysical = status.ullTotalPhys;
            info.availablePhysical = status.ullAvailPhys;
        }
        return info;
    }

    uint64_t ProcessMemoryUsage()
    {
        PROCESS_MEMORY_COUNTERS counters{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
            return counters.WorkingSetSize;
        return 0;
    }

    namespace {
        // The PDH "GPU Process Memory" instance of this process only exists once it has allocated GPU memory, which
        // hasn't happened yet at engine startup. The counters are therefore resolved lazily, retrying
        // PdhExpandWildCardPath until the instance shows up (a wildcard passed straight to PdhAddCounter only binds
        // whatever instance exists at that exact call and is never re-resolved).
        class GpuMemoryCounters {
        public:
            ~GpuMemoryCounters()
            {
                if (m_Query)
                    PdhCloseQuery(m_Query);
            }

            uint64_t Query()
            {
                std::lock_guard<std::mutex> lock(m_Mutex);

                if (!Open())
                    return 0;

                if (!m_Bound && !Bind())
                    return 0;

                if (PdhCollectQueryData(m_Query) != ERROR_SUCCESS)
                    return 0;

                double total = 0.0;
                for (PDH_HCOUNTER counter : m_Counters)
                {
                    PDH_FMT_COUNTERVALUE value{};
                    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) != ERROR_SUCCESS ||
                        value.CStatus != ERROR_SUCCESS)
                    {
                        // The instance can disappear (the app dropped all its GPU allocations) : rebind on the next call.
                        m_Bound = false;
                        m_Counters.clear();
                        continue;
                    }
                    total += value.doubleValue;
                }
                return static_cast<uint64_t>(total);
            }

        private:
            bool Open()
            {
                if (m_Query)
                    return true;
                if (m_Failed)
                    return false;
                if (PdhOpenQueryA(nullptr, 0, &m_Query) != ERROR_SUCCESS)
                {
                    m_Query = nullptr;
                    m_Failed = true;
                    return false;
                }
                // Wildcarded on the "phys_N" suffix : picks up every physical adapter this process has memory on.
                m_Pattern = "\\GPU Process Memory(pid_" + std::to_string(_getpid()) + "_*)\\Dedicated Usage";
                return true;
            }

            bool Bind()
            {
                char paths[4096];
                DWORD size = sizeof(paths);
                if (PdhExpandWildCardPathA(nullptr, m_Pattern.c_str(), paths, &size, 0) != ERROR_SUCCESS)
                    return false;

                for (const char* path = paths; *path != '\0'; path += strlen(path) + 1)
                {
                    PDH_HCOUNTER counter = nullptr;
                    if (PdhAddEnglishCounterA(m_Query, path, 0, &counter) == ERROR_SUCCESS)
                        m_Counters.push_back(counter);
                }
                m_Bound = !m_Counters.empty();
                return m_Bound;
            }

            std::mutex m_Mutex;
            PDH_HQUERY m_Query = nullptr;
            std::string m_Pattern;
            std::vector<PDH_HCOUNTER> m_Counters;
            bool m_Bound = false;
            bool m_Failed = false;
        };
    }

    uint64_t ProcessGpuMemoryUsage()
    {
        static GpuMemoryCounters counters;
        return counters.Query();
    }
}

#endif

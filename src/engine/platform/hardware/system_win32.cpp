#if defined(_WIN32)

#include "engine/platform/hardware/system_info.hpp"
#include "engine/platform/hardware/system_internal.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <process.h>
#include <dxgi.h>

#include <algorithm>
#include <cctype>

#include <mutex>
#include <string>
#include <vector>

namespace Shard::Engine::Core::Platform {

    namespace Internal {

        void QueryOsInfo(OsInfo& out)
        {
            out.name = "Windows";

            // GetVersionEx lies without a manifest, ask ntdll.
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
        // The PDH instance only exists after the process allocates GPU memory. Resolve lazily with PdhExpandWildCardPath, since a wildcard in PdhAddCounter binds once and is never re-resolved.
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

    uint64_t QueryDedicatedVideoMemoryMB(const std::string& adapterName)
    {
        // dxgi.dll is loaded on demand : the engine does not link against it
        using CreateFactoryFn = HRESULT (WINAPI*)(REFIID, void**);
        HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
        if (!dxgi)
            return 0;
        auto create = reinterpret_cast<CreateFactoryFn>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")));
        IDXGIFactory1* factory = nullptr;
        if (!create || FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) || !factory)
        {
            FreeLibrary(dxgi);
            return 0;
        }

        auto normalize = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        };
        const std::string wanted = normalize(adapterName);

        uint64_t result = 0;
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; !result && factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
        {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            {
                std::string name;
                for (const WCHAR* c = desc.Description; *c; ++c)
                    name.push_back(*c < 128 ? char(*c) : '?');
                name = normalize(name);
                if (!name.empty() && !wanted.empty() && (wanted.find(name) != std::string::npos || name.find(wanted) != std::string::npos))
                    result = uint64_t(desc.DedicatedVideoMemory) / (1024 * 1024);
            }
            adapter->Release();
        }

        factory->Release();
        FreeLibrary(dxgi);
        return result;
    }
}

#endif

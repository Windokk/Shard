#if !defined(_WIN32)

#include "engine/platform/hardware/system_info.hpp"
#include "engine/platform/hardware/system_internal.hpp"

#include <sys/utsname.h>
#include <unistd.h>

#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace Shard::Engine::Core::Platform {

    namespace Internal {

        void QueryOsInfo(OsInfo& out)
        {
            utsname info{};
            if (uname(&info) == 0)
            {
                out.name = info.sysname;
                out.version = info.release;
                out.arch = info.machine;
            }
        }

        void QueryCoreCounts(CpuInfo& out)
        {
            // Physical cores = distinct (physical id, core id) pairs of /proc/cpuinfo
            std::ifstream file("/proc/cpuinfo");
            std::string line;
            std::set<std::pair<std::string, std::string>> cores;
            std::string physicalId = "0";
            while (std::getline(file, line))
            {
                const size_t colon = line.find(':');
                if (colon == std::string::npos)
                    continue;
                const std::string value = line.substr(colon + 1 < line.size() ? colon + 2 : colon + 1);
                if (line.rfind("physical id", 0) == 0)
                    physicalId = value;
                else if (line.rfind("core id", 0) == 0)
                    cores.insert({ physicalId, value });
            }
            if (!cores.empty())
                out.physicalCores = static_cast<uint32_t>(cores.size());
        }
    }

    namespace {
        // "MemTotal:  16314988 kB" -> bytes
        uint64_t ReadKiloBytes(const std::string& path, const std::string& key)
        {
            std::ifstream file(path);
            std::string line;
            while (std::getline(file, line))
            {
                if (line.rfind(key, 0) != 0)
                    continue;
                std::istringstream in(line.substr(key.size()));
                uint64_t kb = 0;
                in >> kb;
                return kb * 1024;
            }
            return 0;
        }
    }

    MemoryInfo QueryMemory()
    {
        MemoryInfo info;
        info.totalPhysical = ReadKiloBytes("/proc/meminfo", "MemTotal:");
        info.availablePhysical = ReadKiloBytes("/proc/meminfo", "MemAvailable:");
        return info;
    }

    uint64_t ProcessMemoryUsage()
    {
        return ReadKiloBytes("/proc/self/status", "VmRSS:");
    }

    uint64_t ProcessGpuMemoryUsage()
    {
        // No per-process counter on Linux : the VRAM in use of the first AMD card (sysfs). NVIDIA (NVML) and
        // Intel have no equivalent exposed there.
        std::ifstream vendorFile("/sys/class/drm/card0/device/vendor");
        if (!vendorFile.good())
            return 0;

        std::string vendorHex;
        vendorFile >> vendorHex;
        if (vendorHex != "0x1002")
            return 0;

        uint64_t used = 0;
        std::ifstream file("/sys/class/drm/card0/device/mem_info_vram_used");
        if (file.good())
            file >> used;
        return used;
    }
}

#endif

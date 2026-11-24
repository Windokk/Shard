#pragma once

#include <cstdint>
#include <string>

namespace Shard::Engine::Core::Platform {

    struct CpuFeatures {
        bool sse2 = false, sse41 = false, sse42 = false;
        bool avx = false, avx2 = false, fma = false, avx512f = false;
        bool neon = false;
    };

    struct CpuInfo {
        std::string vendor;
        std::string brand;
        uint32_t logicalCores = 1;
        uint32_t physicalCores = 1;
        CpuFeatures features;
    };

    struct MemoryInfo {
        uint64_t totalPhysical = 0;
        uint64_t availablePhysical = 0;
    };

    struct OsInfo {
        std::string name;
        std::string version;
        std::string arch;
    };

    struct SystemInfo {
        CpuInfo cpu;
        OsInfo os;
        MemoryInfo memory;
    };

    const SystemInfo& GetSystemInfo();

    MemoryInfo QueryMemory();

    uint64_t ProcessMemoryUsage();

    uint64_t ProcessGpuMemoryUsage();

    // Dedicated video memory, in MB, of the adapter whose name `adapterName` (GL_RENDERER, a Vulkan device name) contains or is contained in.
    // 0 when the system cannot tell (no match, or no way to ask : only Windows can) : the caller treats it as unknown.
    uint64_t QueryDedicatedVideoMemoryMB(const std::string& adapterName);
}

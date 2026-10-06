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
        uint64_t totalPhysical = 0;      // bytes
        uint64_t availablePhysical = 0;  // bytes
    };

    struct OsInfo {
        std::string name;       // "Windows", "Linux"
        std::string version;    // "10.0.26200", kernel release
        std::string arch;       // "x86_64", "arm64"
    };

    struct SystemInfo {
        CpuInfo cpu;
        OsInfo os;
        MemoryInfo memory;      // sampled when GetSystemInfo is first called, see QueryMemory for the live values
    };

    /// @brief CPU, OS and memory of the machine. Computed once.
    const SystemInfo& GetSystemInfo();

    /// @brief Live physical memory numbers.
    MemoryInfo QueryMemory();

    /// @brief Physical memory used by this process, in bytes (0 if unknown).
    uint64_t ProcessMemoryUsage();

    /// @brief Dedicated GPU memory used by this process, in bytes (0 when the OS can't tell).
    /// Windows : PDH "GPU Process Memory" counters. Cheap enough to be called every frame.
    uint64_t ProcessGpuMemoryUsage();
}

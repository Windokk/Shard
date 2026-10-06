#include "engine/platform/hardware/system_info.hpp"
#include "engine/platform/hardware/system_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <thread>

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    #define SHARD_X86 1
    #if defined(_MSC_VER)
        #include <intrin.h>
    #else
        #include <cpuid.h>
    #endif
#endif

namespace Shard::Engine::Core::Platform {

    namespace {
#if defined(SHARD_X86)
        struct CpuId { uint32_t eax, ebx, ecx, edx; };

        CpuId Query(uint32_t leaf, uint32_t subleaf = 0)
        {
            CpuId r{};
        #if defined(_MSC_VER)
            int regs[4];
            __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(subleaf));
            r = { (uint32_t)regs[0], (uint32_t)regs[1], (uint32_t)regs[2], (uint32_t)regs[3] };
        #else
            __cpuid_count(leaf, subleaf, r.eax, r.ebx, r.ecx, r.edx);
        #endif
            return r;
        }

        uint64_t ReadXcr0()
        {
        #if defined(_MSC_VER)
            return _xgetbv(0);
        #else
            uint32_t eax, edx;
            __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
            return (static_cast<uint64_t>(edx) << 32) | eax;
        #endif
        }

        std::string Trim(std::string s)
        {
            const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
            s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
            return s;
        }

        void QueryCpuId(CpuInfo& cpu)
        {
            const CpuId leaf0 = Query(0);
            char vendor[13] = {};
            std::memcpy(vendor + 0, &leaf0.ebx, 4);
            std::memcpy(vendor + 4, &leaf0.edx, 4);
            std::memcpy(vendor + 8, &leaf0.ecx, 4);
            cpu.vendor = vendor;

            const CpuId ext = Query(0x80000000u);
            if (ext.eax >= 0x80000004u)
            {
                char brand[49] = {};
                for (uint32_t i = 0; i < 3; ++i)
                {
                    const CpuId r = Query(0x80000002u + i);
                    std::memcpy(brand + i * 16 + 0, &r.eax, 4);
                    std::memcpy(brand + i * 16 + 4, &r.ebx, 4);
                    std::memcpy(brand + i * 16 + 8, &r.ecx, 4);
                    std::memcpy(brand + i * 16 + 12, &r.edx, 4);
                }
                cpu.brand = Trim(brand);
            }

            if (leaf0.eax >= 1)
            {
                const CpuId f1 = Query(1);
                cpu.features.sse2  = (f1.edx >> 26) & 1;
                cpu.features.sse41 = (f1.ecx >> 19) & 1;
                cpu.features.sse42 = (f1.ecx >> 20) & 1;

                // AVX needs the OS to save the YMM registers (OSXSAVE + XCR0)
                const bool osxsave = (f1.ecx >> 27) & 1;
                const uint64_t xcr0 = osxsave ? ReadXcr0() : 0;
                const bool ymm = (xcr0 & 0x6) == 0x6;
                const bool zmm = (xcr0 & 0xE6) == 0xE6;

                cpu.features.avx = ymm && ((f1.ecx >> 28) & 1);
                cpu.features.fma = ymm && ((f1.ecx >> 12) & 1);

                if (leaf0.eax >= 7)
                {
                    const CpuId f7 = Query(7, 0);
                    cpu.features.avx2    = ymm && ((f7.ebx >> 5) & 1);
                    cpu.features.avx512f = zmm && ((f7.ebx >> 16) & 1);
                }
            }
        }
#else
        void QueryCpuId(CpuInfo& cpu)
        {
        #if defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON)
            cpu.features.neon = true;
        #endif
        }
#endif
    }

    const SystemInfo& GetSystemInfo()
    {
        static const SystemInfo info = []
        {
            SystemInfo s;
            s.cpu.logicalCores = std::max(1u, std::thread::hardware_concurrency());
            s.cpu.physicalCores = s.cpu.logicalCores;
            Internal::QueryCoreCounts(s.cpu);
            QueryCpuId(s.cpu);
            Internal::QueryOsInfo(s.os);
            s.memory = QueryMemory();
            return s;
        }();
        return info;
    }
}

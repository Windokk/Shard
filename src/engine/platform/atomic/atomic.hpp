#pragma once

#include <atomic>
#include <cstddef>
#include <new>

#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
    #include <immintrin.h>
#endif

namespace Shard::Engine::Core::Platform {

#if defined(__cpp_lib_hardware_interference_size) && !defined(__GNUC__)
    constexpr size_t kCacheLineSize = std::hardware_destructive_interference_size;
#else
    constexpr size_t kCacheLineSize = 64;
#endif

    inline void CpuRelax()
    {
    #if defined(_MSC_VER)
        _mm_pause();
    #elif defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
    #elif defined(__aarch64__) || defined(__arm__)
        __asm__ volatile("yield");
    #endif
    }

    template <class T>
    struct alignas(kCacheLineSize) CacheLinePadded {
        T value{};
    };

    class SpinLock {
    public:
        void Lock()
        {
            for (;;)
            {
                if (!m_Flag.exchange(true, std::memory_order_acquire))
                    return;
                while (m_Flag.load(std::memory_order_relaxed))
                    CpuRelax();
            }
        }

        bool TryLock() { return !m_Flag.load(std::memory_order_relaxed) && !m_Flag.exchange(true, std::memory_order_acquire); }
        void Unlock() { m_Flag.store(false, std::memory_order_release); }

        void lock() { Lock(); }
        bool try_lock() { return TryLock(); }
        void unlock() { Unlock(); }

    private:
        std::atomic<bool> m_Flag{ false };
    };
}

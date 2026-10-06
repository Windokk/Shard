#pragma once

#include <atomic>
#include <cstddef>
#include <new>

#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
    #include <immintrin.h>
#endif

namespace Shard::Engine::Core::Platform {

    /// @brief Size of a cache line, in bytes. Data written by different threads should not share one (false sharing).
#if defined(__cpp_lib_hardware_interference_size) && !defined(__GNUC__)
    constexpr size_t kCacheLineSize = std::hardware_destructive_interference_size;
#else
    constexpr size_t kCacheLineSize = 64;
#endif

    /// @brief Hint to the CPU that the thread is in a spin-wait loop.
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

    /// @brief A T alone on its cache line.
    template <class T>
    struct alignas(kCacheLineSize) CacheLinePadded {
        T value{};
    };

    /// @brief Test-and-test-and-set spin lock : for critical sections of a few instructions only.
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

        // BasicLockable : usable with std::lock_guard
        void lock() { Lock(); }
        bool try_lock() { return TryLock(); }
        void unlock() { Unlock(); }

    private:
        std::atomic<bool> m_Flag{ false };
    };
}

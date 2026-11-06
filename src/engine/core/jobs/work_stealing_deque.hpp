#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Shard::Engine::Core {

    // Chase-Lev deque (Le, Pop, Cohen, Nardelli 2013). Owner pushes and pops at the bottom (LIFO), thieves steal from the top (oldest, biggest piece). T is a trivially copyable pointer-like type, empty is T{}.
    template <typename T>
    class WorkStealingDeque {
    public:
        explicit WorkStealingDeque(size_t capacityPow2 = 4096) {
            size_t cap = 2;
            while (cap < capacityPow2) cap <<= 1;
            m_Mask = static_cast<int64_t>(cap) - 1;
            m_Buffer = std::make_unique<std::atomic<T>[]>(cap);
            for (size_t i = 0; i < cap; ++i) m_Buffer[i].store(T{}, std::memory_order_relaxed);
        }

        size_t Capacity() const { return static_cast<size_t>(m_Mask) + 1; }

        bool Push(T item) {
            const int64_t b = m_Bottom.load(std::memory_order_relaxed);
            const int64_t t = m_Top.load(std::memory_order_acquire);
            if (b - t > m_Mask) return false;
            m_Buffer[b & m_Mask].store(item, std::memory_order_relaxed);
            // Release store on the atomic itself (the paper uses a release fence), because ThreadSanitizer does not model standalone fences
            m_Bottom.store(b + 1, std::memory_order_release);
            return true;
        }

        T Pop() {
            const int64_t b = m_Bottom.load(std::memory_order_relaxed) - 1;
            m_Bottom.store(b, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            int64_t t = m_Top.load(std::memory_order_relaxed);
            if (t > b) {
                m_Bottom.store(b + 1, std::memory_order_relaxed);
                return T{};
            }
            T item = m_Buffer[b & m_Mask].load(std::memory_order_relaxed);
            if (t == b) {
                if (!m_Top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
                    item = T{};
                m_Bottom.store(b + 1, std::memory_order_relaxed);
            }
            return item;
        }

        T Steal() {
            int64_t t = m_Top.load(std::memory_order_acquire);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            const int64_t b = m_Bottom.load(std::memory_order_acquire);
            if (t >= b) return T{};
            T item = m_Buffer[t & m_Mask].load(std::memory_order_relaxed);
            if (!m_Top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
                return T{};
            return item;
        }

    private:
        // top and bottom are written by different threads: separate cache lines, or every push invalidates every thief
        alignas(64) std::atomic<int64_t> m_Top{0};
        alignas(64) std::atomic<int64_t> m_Bottom{0};
        alignas(64) int64_t m_Mask = 0;
        std::unique_ptr<std::atomic<T>[]> m_Buffer;
    };
}

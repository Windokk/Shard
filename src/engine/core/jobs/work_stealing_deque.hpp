#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Shard::Engine::Core {

    /// @brief Fixed-capacity Chase-Lev work-stealing deque (Le, Pop, Cohen, Nardelli 2013, "Correct and Efficient
    /// Work-Stealing for Weak Memory Models").
    ///
    /// One OWNER thread calls Push / Pop on the "bottom" end (LIFO: the newest job is the hottest in cache, and for
    /// divide & conquer it is the smallest piece). Any number of THIEF threads call Steal on the "top" end (FIFO: the
    /// oldest job is the biggest piece, so one steal moves a lot of work).
    ///
    /// T must be a pointer-like trivially copyable type; the empty value is T{} (nullptr).
    template <typename T>
    class WorkStealingDeque {
    public:
        /// @param capacityPow2 capacity, rounded up to a power of two (indices are masked, not modulo'd).
        explicit WorkStealingDeque(size_t capacityPow2 = 4096) {
            size_t cap = 2;
            while (cap < capacityPow2) cap <<= 1;
            m_Mask = static_cast<int64_t>(cap) - 1;
            m_Buffer = std::make_unique<std::atomic<T>[]>(cap);
            for (size_t i = 0; i < cap; ++i) m_Buffer[i].store(T{}, std::memory_order_relaxed);
        }

        size_t Capacity() const { return static_cast<size_t>(m_Mask) + 1; }

        /// @brief Owner only. Returns false when full (the caller decides what to do: run the job inline).
        bool Push(T item) {
            const int64_t b = m_Bottom.load(std::memory_order_relaxed);
            const int64_t t = m_Top.load(std::memory_order_acquire);
            if (b - t > m_Mask) return false;                                   // b - t == capacity
            m_Buffer[b & m_Mask].store(item, std::memory_order_relaxed);
            // Release store (the paper uses release fence + relaxed store, which is equivalent): everything written
            // before, the slot AND the job it points to, is visible to a thief that reads this bottom with acquire.
            // Expressed on the atomic itself because ThreadSanitizer does not model standalone fences.
            m_Bottom.store(b + 1, std::memory_order_release);
            return true;
        }

        /// @brief Owner only. Takes the newest item, or T{} when empty / when a thief won the race for the last one.
        T Pop() {
            const int64_t b = m_Bottom.load(std::memory_order_relaxed) - 1;
            m_Bottom.store(b, std::memory_order_relaxed);                       // reserve the slot...
            std::atomic_thread_fence(std::memory_order_seq_cst);                // ...and order it against thieves' top read
            int64_t t = m_Top.load(std::memory_order_relaxed);
            if (t > b) {                                                        // was empty
                m_Bottom.store(b + 1, std::memory_order_relaxed);
                return T{};
            }
            T item = m_Buffer[b & m_Mask].load(std::memory_order_relaxed);
            if (t == b) {                                                       // last item: race the thieves for it
                if (!m_Top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
                    item = T{};                                                 // a thief took it
                m_Bottom.store(b + 1, std::memory_order_relaxed);               // deque is empty either way
            }
            return item;
        }

        /// @brief Any thread. Takes the oldest item, or T{} when empty / when it lost a race (the caller just tries
        /// another victim, so a spurious failure costs nothing).
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
        // top and bottom are written by different threads: separate cache lines, or every push invalidates every thief.
        alignas(64) std::atomic<int64_t> m_Top{0};
        alignas(64) std::atomic<int64_t> m_Bottom{0};
        alignas(64) int64_t m_Mask = 0;
        std::unique_ptr<std::atomic<T>[]> m_Buffer;
    };
}

#pragma once

#include <cassert>
#include <new>
#include <type_traits>
#include <utility>

#include "engine/core/memory/alignment.hpp"
#include "engine/core/memory/memory_tracker.hpp"

namespace Shard::Engine::Core {

    /// @brief Bump allocator : Allocate moves a pointer forward, nothing can be freed one by one, Reset frees
    /// everything at once. Allocation is a handful of instructions and objects come out contiguous, which is what
    /// per-frame or per-load scratch data wants.
    ///
    /// Not thread-safe (see FrameAllocator for the concurrent one). Destructors are NEVER run by Reset : only
    /// trivially destructible types can be created with New / NewArray (checked at compile time).
    class LinearAllocator {
    public:
        /// @brief Owns a buffer of `capacity` bytes. `tag` names it in the MemoryTracker.
        explicit LinearAllocator(size_t capacity, const char* tag = "LinearAllocator")
            : m_Capacity(capacity), m_Owns(true) {
            m_Begin = static_cast<unsigned char*>(AlignedAlloc(capacity, kCacheLine));
            assert(m_Begin && "LinearAllocator : out of memory");
            SHARD_TRACK_ALLOC(m_Begin, capacity, tag);
        }

        /// @brief Allocates inside a buffer it does not own (the stack, another allocator...).
        LinearAllocator(void* buffer, size_t capacity)
            : m_Begin(static_cast<unsigned char*>(buffer)), m_Capacity(capacity), m_Owns(false) {}

        ~LinearAllocator() {
            if (m_Owns) {
                SHARD_TRACK_FREE(m_Begin);
                AlignedFree(m_Begin, kCacheLine);
            }
        }

        LinearAllocator(const LinearAllocator&) = delete;
        LinearAllocator& operator=(const LinearAllocator&) = delete;

        /// @brief nullptr when the buffer is full (the allocator never grows).
        void* Allocate(size_t size, size_t alignment = kDefaultAlignment) {
            assert(IsPowerOfTwo(alignment));
            const uintptr_t current = reinterpret_cast<uintptr_t>(m_Begin) + m_Offset;
            const uintptr_t aligned = AlignUp(current, alignment);
            const size_t newOffset = m_Offset + static_cast<size_t>(aligned - current) + size;
            if (newOffset > m_Capacity) return nullptr;
            m_Offset = newOffset;
            if (m_Offset > m_Peak) m_Peak = m_Offset;
            return reinterpret_cast<void*>(aligned);
        }

        template <typename T, typename... Args>
        T* New(Args&&... args) {
            static_assert(std::is_trivially_destructible_v<T>, "Reset() does not run destructors");
            void* memory = Allocate(sizeof(T), alignof(T));
            return memory ? ::new (memory) T(std::forward<Args>(args)...) : nullptr;
        }

        /// @brief `count` default-initialised Ts (value-initialised : zeroed for arithmetic types).
        template <typename T>
        T* NewArray(size_t count) {
            static_assert(std::is_trivially_destructible_v<T>, "Reset() does not run destructors");
            void* memory = Allocate(sizeof(T) * count, alignof(T));
            if (!memory) return nullptr;
            T* array = static_cast<T*>(memory);
            for (size_t i = 0; i < count; ++i) ::new (static_cast<void*>(array + i)) T();
            return array;
        }

        /// @brief Frees everything. Pointers handed out before are dangling.
        void Reset() { m_Offset = 0; }

        size_t Used() const { return m_Offset; }
        size_t Capacity() const { return m_Capacity; }
        size_t Remaining() const { return m_Capacity - m_Offset; }
        size_t PeakUsed() const { return m_Peak; }
        bool Owns(const void* ptr) const {
            return ptr >= m_Begin && ptr < m_Begin + m_Capacity;
        }

    protected:
        static constexpr size_t kCacheLine = 64;

        unsigned char* m_Begin = nullptr;
        size_t m_Capacity = 0;
        size_t m_Offset = 0;
        size_t m_Peak = 0;
        bool m_Owns = false;
    };
}

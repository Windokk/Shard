#pragma once

#include <cassert>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

#include "engine/core/memory/alignment.hpp"
#include "engine/core/memory/memory_tracker.hpp"

namespace Shard::Engine::Core {

    /// @brief Stack allocator : like a linear allocator, but the most recent allocations can be given back, in
    /// reverse order (LIFO). Two ways to do it :
    ///  * Free(ptr) gives back the latest allocation (checked : freeing anything else is a bug and returns false) ;
    ///  * a Marker taken before a group of allocations rewinds all of them at once (ScopedStack does it in a scope).
    ///
    /// Each allocation carries a small header (the offset to go back to), which is what makes Free possible.
    /// Not thread-safe. Like LinearAllocator, it never runs destructors : trivially destructible types only.
    class StackAllocator {
    public:
        using Marker = size_t;

        explicit StackAllocator(size_t capacity, const char* tag = "StackAllocator") : m_Capacity(capacity) {
            m_Begin = static_cast<unsigned char*>(AlignedAlloc(capacity, 64));
            assert(m_Begin && "StackAllocator : out of memory");
            SHARD_TRACK_ALLOC(m_Begin, capacity, tag);
        }

        ~StackAllocator() {
            SHARD_TRACK_FREE(m_Begin);
            AlignedFree(m_Begin, 64);
        }

        StackAllocator(const StackAllocator&) = delete;
        StackAllocator& operator=(const StackAllocator&) = delete;

        /// @brief nullptr when full.
        void* Allocate(size_t size, size_t alignment = kDefaultAlignment) {
            assert(IsPowerOfTwo(alignment));
            const uintptr_t base = reinterpret_cast<uintptr_t>(m_Begin);
            // header, then the payload aligned : the header sits just before the returned pointer
            const uintptr_t afterHeader = base + m_Top + sizeof(Header);
            const uintptr_t aligned = AlignUp(afterHeader, alignment);
            const size_t newTop = static_cast<size_t>(aligned - base) + size;
            if (newTop > m_Capacity) return nullptr;

            Header header{m_Top, m_LastPayload};
            std::memcpy(reinterpret_cast<void*>(aligned - sizeof(Header)), &header, sizeof(Header));
            m_LastPayload = static_cast<size_t>(aligned - base);
            m_Top = newTop;
            if (m_Top > m_Peak) m_Peak = m_Top;
            return reinterpret_cast<void*>(aligned);
        }

        template <typename T, typename... Args>
        T* New(Args&&... args) {
            static_assert(std::is_trivially_destructible_v<T>, "Free / rewind do not run destructors");
            void* memory = Allocate(sizeof(T), alignof(T));
            return memory ? ::new (memory) T(std::forward<Args>(args)...) : nullptr;
        }

        template <typename T>
        T* NewArray(size_t count) {
            static_assert(std::is_trivially_destructible_v<T>, "Free / rewind do not run destructors");
            void* memory = Allocate(sizeof(T) * count, alignof(T));
            if (!memory) return nullptr;
            T* array = static_cast<T*>(memory);
            for (size_t i = 0; i < count; ++i) ::new (static_cast<void*>(array + i)) T();
            return array;
        }

        /// @brief Gives back the latest allocation. Returns false (and does nothing) if `ptr` is not it.
        bool Free(void* ptr) {
            if (!ptr || m_Top == 0) return false;
            const size_t offset = static_cast<size_t>(static_cast<unsigned char*>(ptr) - m_Begin);
            if (offset != m_LastPayload) return false;
            Header header;
            std::memcpy(&header, static_cast<unsigned char*>(ptr) - sizeof(Header), sizeof(Header));
            m_Top = header.previousTop;
            m_LastPayload = header.previousPayload;
            return true;
        }

        /// @brief Where the stack is now. Everything allocated after it is released by RewindTo(marker).
        Marker GetMarker() const { return m_Top; }

        /// @brief Releases every allocation made since `marker`. A marker above the current top (already
        /// rewound past it) is a bug and is ignored.
        void RewindTo(Marker marker) {
            if (marker > m_Top) { assert(false && "StackAllocator::RewindTo : marker is above the top"); return; }
            m_Top = marker;
            // the latest surviving allocation is the one whose payload ends at or before the marker
            m_LastPayload = LastPayloadBefore(marker);
        }

        void Reset() { m_Top = 0; m_LastPayload = kNone; }

        size_t Used() const { return m_Top; }
        size_t Capacity() const { return m_Capacity; }
        size_t PeakUsed() const { return m_Peak; }

    private:
        static constexpr size_t kNone = ~size_t(0);

        struct Header {
            size_t previousTop;         ///< value of m_Top before this allocation
            size_t previousPayload;     ///< payload offset of the allocation before it (kNone for the first)
        };

        /// Walks the chain of headers back from the newest allocation to the last one that starts before `marker`.
        size_t LastPayloadBefore(size_t marker) const {
            size_t payload = m_LastPayload;
            while (payload != kNone && payload > marker) {
                Header header;
                std::memcpy(&header, m_Begin + payload - sizeof(Header), sizeof(Header));
                payload = header.previousPayload;
            }
            return payload;
        }

        unsigned char* m_Begin = nullptr;
        size_t m_Capacity = 0;
        size_t m_Top = 0;
        size_t m_LastPayload = kNone;
        size_t m_Peak = 0;
    };

    /// @brief Rewinds a StackAllocator to where it was when the scope began.
    class ScopedStack {
    public:
        explicit ScopedStack(StackAllocator& stack) : m_Stack(stack), m_Marker(stack.GetMarker()) {}
        ~ScopedStack() { m_Stack.RewindTo(m_Marker); }
        ScopedStack(const ScopedStack&) = delete;
        ScopedStack& operator=(const ScopedStack&) = delete;

    private:
        StackAllocator& m_Stack;
        StackAllocator::Marker m_Marker;
    };
}

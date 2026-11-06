#pragma once

#include <cassert>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

#include "engine/core/memory/alignment.hpp"
#include "engine/core/memory/memory_tracker.hpp"

namespace Shard::Engine::Core {

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

        void* Allocate(size_t size, size_t alignment = kDefaultAlignment) {
            assert(IsPowerOfTwo(alignment));
            const uintptr_t base = reinterpret_cast<uintptr_t>(m_Begin);
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

        Marker GetMarker() const { return m_Top; }

        void RewindTo(Marker marker) {
            if (marker > m_Top) { assert(false && "StackAllocator::RewindTo : marker is above the top"); return; }
            m_Top = marker;
            m_LastPayload = LastPayloadBefore(marker);
        }

        void Reset() { m_Top = 0; m_LastPayload = kNone; }

        size_t Used() const { return m_Top; }
        size_t Capacity() const { return m_Capacity; }
        size_t PeakUsed() const { return m_Peak; }

    private:
        static constexpr size_t kNone = ~size_t(0);

        struct Header {
            size_t previousTop;
            size_t previousPayload;
        };

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

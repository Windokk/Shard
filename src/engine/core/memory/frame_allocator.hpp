#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#include "engine/core/memory/alignment.hpp"
#include "engine/core/memory/memory_tracker.hpp"

namespace Shard::Engine::Core {

    // Ring of frame buffers, lock-free alloc. NextFrame() must not overlap Allocate. Overflow goes to the heap. Trivially destructible types only.
    class FrameAllocator {
    public:
        explicit FrameAllocator(size_t bytesPerFrame, unsigned frames = 2, const char* tag = "FrameAllocator");
        ~FrameAllocator();

        FrameAllocator(const FrameAllocator&) = delete;
        FrameAllocator& operator=(const FrameAllocator&) = delete;

        void* Allocate(size_t size, size_t alignment = kDefaultAlignment);

        template <typename T, typename... Args>
        T* New(Args&&... args) {
            static_assert(std::is_trivially_destructible_v<T>, "NextFrame() does not run destructors");
            void* memory = Allocate(sizeof(T), alignof(T));
            return memory ? ::new (memory) T(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename T>
        T* NewArray(size_t count) {
            static_assert(std::is_trivially_destructible_v<T>, "NextFrame() does not run destructors");
            void* memory = Allocate(sizeof(T) * count, alignof(T));
            if (!memory) return nullptr;
            T* array = static_cast<T*>(memory);
            for (size_t i = 0; i < count; ++i) ::new (static_cast<void*>(array + i)) T();
            return array;
        }

        void NextFrame();

        uint64_t FrameNumber() const { return m_FrameNumber; }
        unsigned FrameCount() const { return m_FrameCount; }
        size_t BytesPerFrame() const { return m_BytesPerFrame; }

        size_t UsedThisFrame() const;
        size_t PeakPerFrame() const { return m_Peak.load(std::memory_order_relaxed); }
        uint64_t OverflowCount() const { return m_OverflowCount.load(std::memory_order_relaxed); }

    private:
        struct Buffer {
            unsigned char* memory = nullptr;
            std::atomic<size_t> offset{0};
            std::mutex overflowMutex;
            std::vector<std::pair<void*, size_t>> overflow;
        };

        void* AllocateOverflow(Buffer& buffer, size_t size, size_t alignment);

        size_t m_BytesPerFrame;
        unsigned m_FrameCount;
        std::vector<Buffer> m_Buffers;
        std::atomic<unsigned> m_Current{0};
        uint64_t m_FrameNumber = 0;
        std::atomic<size_t> m_Peak{0};
        std::atomic<uint64_t> m_OverflowCount{0};
    };
}

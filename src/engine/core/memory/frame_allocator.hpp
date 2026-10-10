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

    /// @brief Per-frame scratch memory that any thread (job, pool thread) can allocate from at the same time.
    ///
    /// It is a ring of `frames` linear buffers (2 by default). Allocate bumps an atomic offset in the buffer of the
    /// CURRENT frame ; NextFrame() moves to the next buffer and clears it. So memory allocated during frame N stays
    /// valid until NextFrame() has been called `frames` times : with 2 frames, the render side can still read the
    /// data the simulation produced in the previous frame while the new frame already allocates.
    ///
    /// When the current buffer is full the allocation falls back to the heap (counted in OverflowCount, freed at the
    /// matching NextFrame) instead of failing : too small a budget shows up in the stats, not as a crash.
    ///
    /// Contract : NextFrame() must not run at the same time as Allocate (call it at the frame boundary, when the
    /// jobs of the previous frame have been waited on). Only trivially destructible types : nothing is destroyed.
    class FrameAllocator {
    public:
        explicit FrameAllocator(size_t bytesPerFrame, unsigned frames = 2, const char* tag = "FrameAllocator");
        ~FrameAllocator();

        FrameAllocator(const FrameAllocator&) = delete;
        FrameAllocator& operator=(const FrameAllocator&) = delete;

        /// @brief Never nullptr (except on a real out-of-memory).
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

        /// @brief Starts the next frame : its buffer is cleared, the allocations made `frames` frames ago are gone.
        void NextFrame();

        uint64_t FrameNumber() const { return m_FrameNumber; }
        unsigned FrameCount() const { return m_FrameCount; }
        size_t BytesPerFrame() const { return m_BytesPerFrame; }

        /// Bytes taken from the current frame's buffer so far (heap overflow not included).
        size_t UsedThisFrame() const;
        /// Largest UsedThisFrame seen at the end of a frame : the budget actually needed.
        size_t PeakPerFrame() const { return m_Peak.load(std::memory_order_relaxed); }
        /// Allocations that did not fit and went to the heap, since the start.
        uint64_t OverflowCount() const { return m_OverflowCount.load(std::memory_order_relaxed); }

    private:
        struct Buffer {
            unsigned char* memory = nullptr;
            std::atomic<size_t> offset{0};
            std::mutex overflowMutex;
            std::vector<std::pair<void*, size_t>> overflow;   // heap blocks of this frame, with their alignment
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

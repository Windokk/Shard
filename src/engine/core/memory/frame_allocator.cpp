#include "engine/core/memory/frame_allocator.hpp"

#include <algorithm>
#include <cassert>

namespace Shard::Engine::Core {

    namespace {
        constexpr size_t kBufferAlignment = 64;
    }

    FrameAllocator::FrameAllocator(size_t bytesPerFrame, unsigned frames, const char* tag)
        : m_BytesPerFrame(bytesPerFrame), m_FrameCount(std::max(1u, frames)), m_Buffers(std::max(1u, frames)) {
        for (Buffer& buffer : m_Buffers) {
            buffer.memory = static_cast<unsigned char*>(AlignedAlloc(bytesPerFrame, kBufferAlignment));
            assert(buffer.memory && "FrameAllocator : out of memory");
            SHARD_TRACK_ALLOC(buffer.memory, bytesPerFrame, tag);
        }
    }

    FrameAllocator::~FrameAllocator() {
        for (Buffer& buffer : m_Buffers) {
            for (auto& [block, align] : buffer.overflow) AlignedFree(block, align);
            SHARD_TRACK_FREE(buffer.memory);
            AlignedFree(buffer.memory, kBufferAlignment);
        }
    }

    void* FrameAllocator::Allocate(size_t size, size_t alignment) {
        assert(IsPowerOfTwo(alignment));
        Buffer& buffer = m_Buffers[m_Current.load(std::memory_order_relaxed)];

        // Lock-free bump : claim [aligned, aligned + size) with a compare-and-swap on the offset
        const uintptr_t base = reinterpret_cast<uintptr_t>(buffer.memory);
        size_t offset = buffer.offset.load(std::memory_order_relaxed);
        for (;;) {
            const size_t aligned = static_cast<size_t>(AlignUp(base + offset, alignment) - base);
            const size_t end = aligned + size;
            if (end > m_BytesPerFrame) return AllocateOverflow(buffer, size, alignment);
            if (buffer.offset.compare_exchange_weak(offset, end, std::memory_order_relaxed))
                return buffer.memory + aligned;
            // offset was refreshed by the failed CAS : retry
        }
    }

    void* FrameAllocator::AllocateOverflow(Buffer& buffer, size_t size, size_t alignment) {
        m_OverflowCount.fetch_add(1, std::memory_order_relaxed);
        const size_t blockAlignment = std::max(alignment, kDefaultAlignment);
        void* block = AlignedAlloc(size, blockAlignment);
        if (!block) return nullptr;
        std::lock_guard<std::mutex> lock(buffer.overflowMutex);
        buffer.overflow.emplace_back(block, blockAlignment);
        return block;
    }

    size_t FrameAllocator::UsedThisFrame() const {
        return std::min(m_Buffers[m_Current.load(std::memory_order_relaxed)].offset.load(std::memory_order_relaxed),
                        m_BytesPerFrame);
    }

    void FrameAllocator::NextFrame() {
        // The budget actually needed by the frame that just ended
        size_t used = UsedThisFrame();
        size_t peak = m_Peak.load(std::memory_order_relaxed);
        while (used > peak && !m_Peak.compare_exchange_weak(peak, used, std::memory_order_relaxed)) {}

        const unsigned next = (m_Current.load(std::memory_order_relaxed) + 1) % m_FrameCount;
        Buffer& buffer = m_Buffers[next];
        for (auto& [block, align] : buffer.overflow) AlignedFree(block, align);
        buffer.overflow.clear();
        buffer.offset.store(0, std::memory_order_relaxed);

        m_Current.store(next, std::memory_order_release);
        ++m_FrameNumber;
    }
}

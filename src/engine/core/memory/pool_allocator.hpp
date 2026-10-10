#pragma once

#include <cassert>
#include <cstdint>
#include <new>
#include <utility>
#include <vector>

#include "engine/core/memory/alignment.hpp"
#include "engine/core/memory/memory_tracker.hpp"

namespace Shard::Engine::Core {

    /// @brief Fixed-size block allocator : `blockCount` blocks of `blockSize` bytes carved out of one buffer.
    /// Allocate and Free are O(1) and never fragment, because every block is interchangeable. The free blocks form
    /// a linked list threaded through their own memory, so the bookkeeping costs one byte per block (its state, used
    /// to catch double frees and foreign pointers).
    ///
    /// Not thread-safe. Out of blocks : Allocate returns nullptr (the pool never grows).
    class PoolAllocator {
    public:
        PoolAllocator(size_t blockSize, size_t blockCount, size_t alignment = kDefaultAlignment,
                      const char* tag = "PoolAllocator")
            : m_BlockCount(blockCount), m_Alignment(alignment) {
            assert(IsPowerOfTwo(alignment));
            m_BlockSize = AlignUp(blockSize < sizeof(void*) ? sizeof(void*) : blockSize, alignment);
            m_Memory = static_cast<unsigned char*>(AlignedAlloc(m_BlockSize * blockCount, alignment));
            assert(m_Memory && "PoolAllocator : out of memory");
            SHARD_TRACK_ALLOC(m_Memory, m_BlockSize * blockCount, tag);
            m_Used.assign(blockCount, 0);
            Reset();
        }

        ~PoolAllocator() {
            SHARD_TRACK_FREE(m_Memory);
            AlignedFree(m_Memory, m_Alignment);
        }

        PoolAllocator(const PoolAllocator&) = delete;
        PoolAllocator& operator=(const PoolAllocator&) = delete;

        /// @brief One block, or nullptr when all are taken.
        void* Allocate() {
            if (!m_FreeList) return nullptr;
            FreeBlock* block = m_FreeList;
            m_FreeList = block->next;
            m_Used[IndexOf(block)] = 1;
            ++m_InUse;
            if (m_InUse > m_Peak) m_Peak = m_InUse;
            return block;
        }

        /// @brief Gives a block back. Returns false (and does nothing) for a pointer that is not a block of this
        /// pool, or a block that is already free.
        bool Free(void* ptr) {
            if (!Owns(ptr)) return false;
            const size_t offset = static_cast<size_t>(static_cast<unsigned char*>(ptr) - m_Memory);
            if (offset % m_BlockSize != 0) return false;                 // inside a block, not at its start
            const size_t index = offset / m_BlockSize;
            if (!m_Used[index]) return false;                            // double free
            m_Used[index] = 0;
            FreeBlock* block = static_cast<FreeBlock*>(ptr);
            block->next = m_FreeList;
            m_FreeList = block;
            --m_InUse;
            return true;
        }

        /// @brief Every block becomes free again (objects living in them are not destroyed).
        void Reset() {
            m_FreeList = nullptr;
            for (size_t i = m_BlockCount; i-- > 0;) {                    // lowest address first on the list
                FreeBlock* block = reinterpret_cast<FreeBlock*>(m_Memory + i * m_BlockSize);
                block->next = m_FreeList;
                m_FreeList = block;
                m_Used[i] = 0;
            }
            m_InUse = 0;
        }

        bool Owns(const void* ptr) const {
            return ptr >= m_Memory && ptr < m_Memory + m_BlockSize * m_BlockCount;
        }
        bool IsInUse(const void* ptr) const {
            if (!Owns(ptr)) return false;
            const size_t offset = static_cast<size_t>(static_cast<const unsigned char*>(ptr) - m_Memory);
            return offset % m_BlockSize == 0 && m_Used[offset / m_BlockSize] != 0;
        }

        /// @brief Address of block `index` (0 <= index < BlockCount()), free or not.
        void* BlockAt(size_t index) const { return m_Memory + index * m_BlockSize; }

        size_t BlockSize() const { return m_BlockSize; }
        size_t BlockCount() const { return m_BlockCount; }
        size_t InUse() const { return m_InUse; }
        size_t PeakInUse() const { return m_Peak; }
        bool Empty() const { return m_InUse == 0; }
        bool Full() const { return m_InUse == m_BlockCount; }

    private:
        struct FreeBlock { FreeBlock* next; };

        size_t IndexOf(const void* block) const {
            return static_cast<size_t>(static_cast<const unsigned char*>(block) - m_Memory) / m_BlockSize;
        }

        unsigned char* m_Memory = nullptr;
        size_t m_BlockSize = 0;
        size_t m_BlockCount = 0;
        size_t m_Alignment = 0;
        FreeBlock* m_FreeList = nullptr;
        std::vector<uint8_t> m_Used;
        size_t m_InUse = 0;
        size_t m_Peak = 0;
    };

    /// @brief A pool of objects of type T : Create constructs one in a free block, Destroy destroys it and gives the
    /// block back. Objects still alive when the pool is destroyed are destroyed with it.
    template <typename T>
    class ObjectPool {
    public:
        explicit ObjectPool(size_t capacity, const char* tag = "ObjectPool")
            : m_Pool(sizeof(T), capacity, alignof(T), tag) {}

        ~ObjectPool() {
            for (size_t i = 0; i < m_Pool.BlockCount(); ++i) {
                void* block = m_Pool.BlockAt(i);
                if (m_Pool.IsInUse(block)) static_cast<T*>(block)->~T();
            }
        }

        ObjectPool(const ObjectPool&) = delete;
        ObjectPool& operator=(const ObjectPool&) = delete;

        /// @brief nullptr when the pool is full.
        template <typename... Args>
        T* Create(Args&&... args) {
            void* memory = m_Pool.Allocate();
            return memory ? ::new (memory) T(std::forward<Args>(args)...) : nullptr;
        }

        /// @brief Destroys the object and recycles its block. False for a pointer that is not a live object of this pool.
        bool Destroy(T* object) {
            if (!m_Pool.IsInUse(object)) return false;
            object->~T();
            return m_Pool.Free(object);
        }

        size_t Size() const { return m_Pool.InUse(); }
        size_t Capacity() const { return m_Pool.BlockCount(); }
        bool Owns(const T* object) const { return m_Pool.Owns(object); }

    private:
        PoolAllocator m_Pool;
    };
}

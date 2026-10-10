#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/core/ecs/component.hpp"
#include "engine/core/ecs/entity.hpp"

namespace Shard::Engine::Core::Ecs {

    /// @brief All the entities that have exactly the same set of components, stored column by column in fixed-size chunks.
    ///
    /// A chunk is one block of memory laid out as   [Entity x N][component A x N][component B x N]...   (structure of arrays).
    /// A system that reads Position and Velocity walks two tightly packed arrays : no pointer chasing, no virtual call, and the
    /// compiler can vectorise the loop. N (the chunk capacity) is the largest count whose columns fit in kChunkBytes.
    ///
    /// Rows are numbered across the chunks of the archetype : row r lives in chunk r / capacity, slot r % capacity. Removing a
    /// row moves the LAST row into the hole (swap-remove), so rows stay dense ; the caller is told which entity moved.
    ///
    /// Not thread-safe for structural changes. Reading and writing different rows from several threads is fine.
    class Archetype {
    public:
        static constexpr size_t kChunkBytes = 16 * 1024;

        explicit Archetype(ComponentSet signature);
        ~Archetype();
        Archetype(const Archetype&) = delete;
        Archetype& operator=(const Archetype&) = delete;

        const ComponentSet& Signature() const { return m_Signature; }
        bool Has(ComponentId id) const { return Contains(m_Signature, id); }

        size_t Count() const { return m_Count; }
        size_t Capacity() const { return m_Capacity; }
        /// Bytes of one chunk
        size_t ChunkAllocSize() const { return m_ChunkAllocSize; }
        size_t ChunkCount() const { return m_Chunks.size(); }
        /// Entities in chunk `chunk` (all chunks are full except the last)
        size_t ChunkSize(size_t chunk) const { return m_Chunks[chunk].count; }

        Entity* ChunkEntities(size_t chunk) const { return reinterpret_cast<Entity*>(m_Chunks[chunk].data); }
        /// The column of component `id` in a chunk, or nullptr if the archetype doesn't have it
        void* ChunkColumn(size_t chunk, ComponentId id) const;

        /// Appends a row for `entity`; its component bytes are uninitialised (the caller fills every column). Returns the row.
        uint32_t Push(Entity entity);

        /// Removes `row` by moving the last row into it. Returns the entity that now lives at `row`, or the null
        /// entity if `row` was the last one (nothing moved).
        Entity RemoveSwap(uint32_t row);

        Entity EntityAt(uint32_t row) const { return ChunkEntities(row / m_Capacity)[row % m_Capacity]; }
        /// Address of a component of a row, nullptr if the archetype doesn't have it
        void* ComponentPtr(uint32_t row, ComponentId id) const;

        /// Copies every component the two archetypes have in common from (src, srcRow) to (this, dstRow)
        void CopyCommon(uint32_t dstRow, const Archetype& src, uint32_t srcRow) const;

        // Cached transitions ("this archetype + component X" / "- component X"), so adding a component doesn't re-search by signature
        Archetype* AddEdge(ComponentId id) const;
        Archetype* RemoveEdge(ComponentId id) const;
        void SetAddEdge(ComponentId id, Archetype* target);
        void SetRemoveEdge(ComponentId id, Archetype* target);

    private:
        struct Chunk {
            uint8_t* data = nullptr;
            uint32_t count = 0;
        };

        struct Column {
            ComponentId id;
            size_t size;
            size_t offset;      // from the start of a chunk
        };

        const Column* FindColumn(ComponentId id) const;

        ComponentSet m_Signature;
        std::vector<Column> m_Columns;          // same order as m_Signature
        std::vector<Chunk> m_Chunks;
        size_t m_Capacity = 1;
        size_t m_ChunkAllocSize = 0;
        size_t m_Count = 0;

        mutable std::vector<std::pair<ComponentId, Archetype*>> m_AddEdges;
        mutable std::vector<std::pair<ComponentId, Archetype*>> m_RemoveEdges;
    };
}

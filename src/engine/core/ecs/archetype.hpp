#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/core/ecs/component.hpp"
#include "engine/core/ecs/entity.hpp"

namespace Shard::Engine::Core::Ecs {

    // Entities with the same component set, stored per column in fixed-size chunks (SoA). Swap-remove keeps rows dense and reports the moved entity. Not thread-safe for structural changes.
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
        size_t ChunkAllocSize() const { return m_ChunkAllocSize; }
        size_t ChunkCount() const { return m_Chunks.size(); }
        size_t ChunkSize(size_t chunk) const { return m_Chunks[chunk].count; }

        Entity* ChunkEntities(size_t chunk) const { return reinterpret_cast<Entity*>(m_Chunks[chunk].data); }
        void* ChunkColumn(size_t chunk, ComponentId id) const;

        uint32_t Push(Entity entity);

        Entity RemoveSwap(uint32_t row);

        Entity EntityAt(uint32_t row) const { return ChunkEntities(row / m_Capacity)[row % m_Capacity]; }
        void* ComponentPtr(uint32_t row, ComponentId id) const;
        size_t ComponentSize(ComponentId id) const { const Column* column = FindColumn(id); return column ? column->size : 0; }

        void CopyCommon(uint32_t dstRow, const Archetype& src, uint32_t srcRow) const;

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
            size_t offset;
        };

        const Column* FindColumn(ComponentId id) const;

        ComponentSet m_Signature;
        std::vector<Column> m_Columns;
        std::vector<Chunk> m_Chunks;
        size_t m_Capacity = 1;
        size_t m_ChunkAllocSize = 0;
        size_t m_Count = 0;

        mutable std::vector<std::pair<ComponentId, Archetype*>> m_AddEdges;
        mutable std::vector<std::pair<ComponentId, Archetype*>> m_RemoveEdges;
    };
}

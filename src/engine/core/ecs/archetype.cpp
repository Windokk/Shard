#include "engine/core/ecs/archetype.hpp"

#include <algorithm>
#include <cstring>

#include "engine/core/memory/alignment.hpp"

namespace Shard::Engine::Core::Ecs {

    namespace {
        constexpr size_t kChunkAlignment = 64;      // a cache line : columns start aligned, chunks never share a line

        /// Bytes needed by a chunk of `capacity` rows : the entity column, then each column aligned to its type
        size_t LayoutSize(const ComponentSet& signature, size_t capacity, std::vector<size_t>* offsets) {
            size_t cursor = capacity * sizeof(Entity);
            ComponentRegistry& registry = ComponentRegistry::Global();
            for (ComponentId id : signature) {
                const ComponentInfo& info = registry.Info(id);
                cursor = AlignUp(cursor, info.align);
                if (offsets) offsets->push_back(cursor);
                cursor += capacity * info.size;
            }
            return cursor;
        }
    }

    Archetype::Archetype(ComponentSet signature) : m_Signature(std::move(signature)) {
        Normalize(m_Signature);

        // Largest capacity whose columns fit in a chunk. At least one row, even if a single entity is bigger than a chunk.
        size_t capacity = 1;
        {
            size_t lo = 1, hi = kChunkBytes / sizeof(Entity);
            while (lo < hi) {                                               // binary search : LayoutSize grows with capacity
                const size_t mid = (lo + hi + 1) / 2;
                if (LayoutSize(m_Signature, mid, nullptr) <= kChunkBytes) lo = mid; else hi = mid - 1;
            }
            capacity = lo;
        }
        m_Capacity = capacity;

        std::vector<size_t> offsets;
        m_ChunkAllocSize = LayoutSize(m_Signature, m_Capacity, &offsets);
        ComponentRegistry& registry = ComponentRegistry::Global();
        for (size_t i = 0; i < m_Signature.size(); ++i)
            m_Columns.push_back(Column{m_Signature[i], registry.Info(m_Signature[i]).size, offsets[i]});
    }

    Archetype::~Archetype() {
        for (Chunk& chunk : m_Chunks) AlignedFree(chunk.data, kChunkAlignment);
    }

    const Archetype::Column* Archetype::FindColumn(ComponentId id) const {
        size_t lo = 0, hi = m_Columns.size();
        while (lo < hi) {
            const size_t mid = (lo + hi) / 2;
            if (m_Columns[mid].id < id) lo = mid + 1; else hi = mid;
        }
        return (lo < m_Columns.size() && m_Columns[lo].id == id) ? &m_Columns[lo] : nullptr;
    }

    void* Archetype::ChunkColumn(size_t chunk, ComponentId id) const {
        const Column* column = FindColumn(id);
        return column ? m_Chunks[chunk].data + column->offset : nullptr;
    }

    uint32_t Archetype::Push(Entity entity) {
        if (m_Chunks.empty() || m_Chunks.back().count == m_Capacity) {
            Chunk chunk;
            chunk.data = static_cast<uint8_t*>(AlignedAlloc(m_ChunkAllocSize, kChunkAlignment));
            std::memset(chunk.data, 0, m_ChunkAllocSize);
            m_Chunks.push_back(chunk);
        }
        Chunk& chunk = m_Chunks.back();
        const uint32_t row = static_cast<uint32_t>((m_Chunks.size() - 1) * m_Capacity + chunk.count);
        reinterpret_cast<Entity*>(chunk.data)[chunk.count] = entity;
        ++chunk.count;
        ++m_Count;
        return row;
    }

    Entity Archetype::RemoveSwap(uint32_t row) {
        const uint32_t last = static_cast<uint32_t>(m_Count - 1);
        Entity moved;
        if (row != last) {
            Chunk& dstChunk = m_Chunks[row / m_Capacity];
            Chunk& srcChunk = m_Chunks[last / m_Capacity];
            const size_t dst = row % m_Capacity, src = last % m_Capacity;
            Entity* dstEntities = reinterpret_cast<Entity*>(dstChunk.data);
            Entity* srcEntities = reinterpret_cast<Entity*>(srcChunk.data);
            dstEntities[dst] = srcEntities[src];
            moved = dstEntities[dst];
            for (const Column& column : m_Columns)
                std::memcpy(dstChunk.data + column.offset + dst * column.size, srcChunk.data + column.offset + src * column.size, column.size);
        }

        Chunk& lastChunk = m_Chunks.back();
        --lastChunk.count;
        --m_Count;
        if (lastChunk.count == 0) {                                          // the tail chunk emptied : give its memory back
            AlignedFree(lastChunk.data, kChunkAlignment);
            m_Chunks.pop_back();
        }
        return moved;
    }

    void* Archetype::ComponentPtr(uint32_t row, ComponentId id) const {
        const Column* column = FindColumn(id);
        if (!column) return nullptr;
        return m_Chunks[row / m_Capacity].data + column->offset + (row % m_Capacity) * column->size;
    }

    void Archetype::CopyCommon(uint32_t dstRow, const Archetype& src, uint32_t srcRow) const {
        for (const Column& column : m_Columns) {
            const void* from = src.ComponentPtr(srcRow, column.id);
            if (from) std::memcpy(ComponentPtr(dstRow, column.id), from, column.size);
        }
    }

    namespace {
        Archetype* FindEdge(const std::vector<std::pair<ComponentId, Archetype*>>& edges, ComponentId id) {
            for (const auto& edge : edges) if (edge.first == id) return edge.second;
            return nullptr;
        }
    }

    Archetype* Archetype::AddEdge(ComponentId id) const { return FindEdge(m_AddEdges, id); }
    Archetype* Archetype::RemoveEdge(ComponentId id) const { return FindEdge(m_RemoveEdges, id); }
    void Archetype::SetAddEdge(ComponentId id, Archetype* target) { m_AddEdges.emplace_back(id, target); }
    void Archetype::SetRemoveEdge(ComponentId id, Archetype* target) { m_RemoveEdges.emplace_back(id, target); }
}

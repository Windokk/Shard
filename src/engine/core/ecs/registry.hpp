#pragma once

#include <atomic>
#include <cassert>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "engine/core/ecs/archetype.hpp"
#include "engine/core/ecs/command_buffer.hpp"
#include "engine/core/ecs/component.hpp"
#include "engine/core/ecs/entity.hpp"
#include "engine/core/jobs/job_system.hpp"
#include "engine/core/jobs/parallel_for.hpp"

namespace Shard::Engine::Core::Ecs {

    template <typename... Ts> struct Exclude {};

    // Components are trivially copyable. Structural changes are refused during a query or parallel phase (use CommandBuffer or DestroyDeferred).
    // Changing a value or the parent is always allowed.
    class Registry {
    public:
        Registry();
        ~Registry();
        Registry(const Registry&) = delete;
        Registry& operator=(const Registry&) = delete;

        Entity Create();

        template <typename... Cs>
        Entity Create(const Cs&... values) {
            static_assert((std::is_trivially_copyable<Cs>::value && ...), "components must be trivially copyable (POD-like)");
            if (!CanChangeStructure()) return kNullEntity;
            ComponentSet signature{ComponentIdOf<Cs>()...};
            Normalize(signature);
            Archetype* archetype = FindOrCreateArchetype(signature);
            const Entity e = AllocateEntity();
            Record& record = m_Records[e.Index()];
            record.archetype = archetype;
            record.row = archetype->Push(e);
            (void)std::initializer_list<int>{(WriteValue(record, values), 0)...};
            return e;
        }

        bool Destroy(Entity e);

        bool IsAlive(Entity e) const;
        size_t Count() const { return m_AliveCount; }

        uint64_t StructuralVersion() const { return m_StructuralVersion; }

        template <typename T>
        bool Has(Entity e) const { return HasRaw(e, ComponentIdOf<T>()); }

        template <typename T>
        T* TryGet(Entity e) { return static_cast<T*>(GetRaw(e, ComponentIdOf<T>())); }
        template <typename T>
        const T* TryGet(Entity e) const { return static_cast<const T*>(GetRaw(e, ComponentIdOf<T>())); }

        template <typename T>
        T& Get(Entity e) { T* p = TryGet<T>(e); assert(p && "entity has no such component"); return *p; }
        template <typename T>
        const T& Get(Entity e) const { const T* p = TryGet<T>(e); assert(p && "entity has no such component"); return *p; }

        template <typename T>
        bool Add(Entity e, const T& value) {
            static_assert(std::is_trivially_copyable<T>::value, "components must be trivially copyable (POD-like)");
            return AddRaw(e, ComponentIdOf<T>(), &value);
        }

        template <typename T>
        bool Remove(Entity e) { return RemoveRaw(e, ComponentIdOf<T>()); }

        bool HasRaw(Entity e, ComponentId id) const;
        void* GetRaw(Entity e, ComponentId id) const;
        bool AddRaw(Entity e, ComponentId id, const void* data);
        bool RemoveRaw(Entity e, ComponentId id);

        bool SetParent(Entity child, Entity parent);
        Entity GetParent(Entity e) const;
        Entity FirstChild(Entity e) const;
        Entity NextSibling(Entity e) const;
        size_t ChildCount(Entity e) const;
        bool IsAncestor(Entity ancestor, Entity e) const;

        template <typename F>
        void ForEachChild(Entity parent, F&& fn) const {
            for (Entity c = FirstChild(parent); c; c = NextSibling(c)) fn(c);
        }

        template <typename F>
        void ForEachDescendant(Entity root, F&& fn) const {
            std::vector<Entity> stack;
            PushChildrenReversed(root, stack);
            while (!stack.empty()) {
                const Entity e = stack.back();
                stack.pop_back();
                fn(e);
                PushChildrenReversed(e, stack);
            }
        }

        template <typename... Ts, typename F>
        void Each(F&& fn) { EachExcluding<Exclude<>, Ts...>(std::forward<F>(fn)); }

        template <typename Excluded, typename... Ts, typename F>
        void EachExcluding(F&& fn) {
            IterationScope scope(*this);
            ForEachChunkImpl<Excluded, Ts...>([&fn](size_t n, Entity* entities, std::remove_cv_t<Ts>*... columns) {
                for (size_t i = 0; i < n; ++i) CallRow<F, Ts...>(fn, entities[i], columns[i]...);
            });
        }

        template <typename... Ts, typename F>
        void EachChunk(F&& fn) {
            IterationScope scope(*this);
            ForEachChunkImpl<Exclude<>, Ts...>([&fn](size_t n, Entity* entities, std::remove_cv_t<Ts>*... columns) {
                fn(n, static_cast<const Entity*>(entities), columns...);
            });
        }

        template <typename... Ts, typename F>
        void ParallelEach(JobSystem* js, F&& fn) {
            IterationScope scope(*this);
            ComponentSet required{ComponentIdOf<std::remove_cv_t<Ts>>()...};
            Normalize(required);
            std::vector<std::pair<Archetype*, size_t>> chunks;
            for (Archetype* archetype : Matching(required, ComponentSet{}))
                for (size_t c = 0; c < archetype->ChunkCount(); ++c) chunks.emplace_back(archetype, c);

            auto runChunk = [&fn](Archetype* archetype, size_t chunk) {
                RunChunk<std::remove_cv_t<Ts>...>(archetype, chunk, [&fn](size_t n, Entity* entities, std::remove_cv_t<Ts>*... columns) {
                    for (size_t i = 0; i < n; ++i) CallRow<F, Ts...>(fn, entities[i], columns[i]...);
                });
            };
            if (!js || chunks.size() < 2) {
                for (auto& [archetype, chunk] : chunks) runChunk(archetype, chunk);
                return;
            }
            ParallelFor(*js, 0, chunks.size(), 1, [&](size_t i) { runChunk(chunks[i].first, chunks[i].second); });
        }

        template <typename F>
        void EachChunkRaw(const std::vector<ComponentId>& with, const ComponentSet& without, F&& fn) {
            IterationScope scope(*this);
            ComponentSet required = with;
            Normalize(required);
            ComponentSet excluded = without;
            Normalize(excluded);
            std::vector<void*> columns(with.size(), nullptr);
            for (Archetype* archetype : Matching(required, excluded)) {
                for (size_t c = 0; c < archetype->ChunkCount(); ++c) {
                    if (archetype->ChunkSize(c) == 0) continue;
                    for (size_t i = 0; i < with.size(); ++i) columns[i] = archetype->ChunkColumn(c, with[i]);
                    if (!fn(archetype->ChunkSize(c), static_cast<const Entity*>(archetype->ChunkEntities(c)), static_cast<void* const*>(columns.data()))) return;
                }
            }
        }

        size_t CountRaw(const std::vector<ComponentId>& with, const ComponentSet& without) const {
            ComponentSet required = with;
            Normalize(required);
            ComponentSet excluded = without;
            Normalize(excluded);
            size_t total = 0;
            for (Archetype* archetype : Matching(required, excluded)) total += archetype->Count();
            return total;
        }

        template <typename... Ts>
        size_t CountOf() const {
            ComponentSet required{ComponentIdOf<std::remove_cv_t<Ts>>()...};
            Normalize(required);
            size_t total = 0;
            for (Archetype* archetype : Matching(required, ComponentSet{})) total += archetype->Count();
            return total;
        }

        size_t ArchetypeCount() const { return m_ArchetypeList.size(); }

        struct PlaybackResult {
            size_t applied = 0;
            size_t skipped = 0;
        };

        PlaybackResult Playback(CommandBuffer& buffer);

        bool IsIterating() const { return m_Iterating.load(std::memory_order_acquire) > 0; }

        void BeginConcurrent() { m_Concurrent.fetch_add(1, std::memory_order_acq_rel); }
        void EndConcurrent() { m_Concurrent.fetch_sub(1, std::memory_order_acq_rel); }
        bool IsConcurrent() const { return m_Concurrent.load(std::memory_order_acquire) > 0; }

        bool CanChangeStructure() const { return !IsIterating() && !IsConcurrent(); }

        void DestroyDeferred(Entity e);
        size_t FlushDeferred();

        struct Stats {
            size_t entities = 0;
            size_t archetypes = 0;
            size_t chunks = 0;
            size_t chunkBytes = 0;
            size_t pendingDestroys = 0;
        };
        Stats GetStats() const;
        std::string Describe() const;

    private:
        struct Record {
            uint32_t generation = 1;
            bool alive = false;
            Archetype* archetype = nullptr;
            uint32_t row = 0;
            Entity parent, firstChild, lastChild, prev, next;
            uint32_t childCount = 0;
        };

        struct IterationScope {
            explicit IterationScope(Registry& r) : registry(r) { registry.m_Iterating.fetch_add(1, std::memory_order_acq_rel); }
            ~IterationScope() { registry.m_Iterating.fetch_sub(1, std::memory_order_acq_rel); }
            Registry& registry;
        };

        template <typename T>
        void WriteValue(Record& record, const T& value) {
            std::memcpy(record.archetype->ComponentPtr(record.row, ComponentIdOf<T>()), &value, sizeof(T));
        }

        template <typename F, typename... Ts, typename... Raws>
        static void CallRow(F& fn, Entity e, Raws&... components) {
            if constexpr (std::is_invocable<F&, Entity, Ts&...>::value) fn(e, components...);
            else fn(components...);
        }

        template <typename... Raws, typename Body>
        static void RunChunk(Archetype* archetype, size_t chunk, Body&& body) {
            body(archetype->ChunkSize(chunk), archetype->ChunkEntities(chunk),
                 static_cast<Raws*>(archetype->ChunkColumn(chunk, ComponentIdOf<Raws>()))...);
        }

        template <typename Excluded, typename... Ts, typename Body>
        void ForEachChunkImpl(Body&& body) {
            ComponentSet required{ComponentIdOf<std::remove_cv_t<Ts>>()...};
            Normalize(required);
            ComponentSet excluded = ExcludedIds(Excluded{});
            for (Archetype* archetype : Matching(required, excluded))
                for (size_t c = 0; c < archetype->ChunkCount(); ++c)
                    RunChunk<std::remove_cv_t<Ts>...>(archetype, c, body);
        }

        template <typename... Xs>
        static ComponentSet ExcludedIds(Exclude<Xs...>) {
            ComponentSet set{ComponentIdOf<Xs>()...};
            Normalize(set);
            return set;
        }

        void PushChildrenReversed(Entity parent, std::vector<Entity>& stack) const;

        std::vector<Archetype*> Matching(const ComponentSet& required, const ComponentSet& excluded) const;
        Archetype* FindOrCreateArchetype(const ComponentSet& signature);
        Archetype* ArchetypeWith(Archetype* from, ComponentId id);
        Archetype* ArchetypeWithout(Archetype* from, ComponentId id);

        Entity AllocateEntity();
        void ReleaseEntity(Entity e);
        void MoveTo(Entity e, Archetype* target);
        void EraseFromArchetype(Record& record);
        void Unlink(Entity e);
        const Record* RecordOf(Entity e) const;
        Record* RecordOf(Entity e);

        std::vector<Record> m_Records;
        std::vector<uint32_t> m_FreeIndices;
        std::map<ComponentSet, std::unique_ptr<Archetype>> m_Archetypes;
        std::vector<Archetype*> m_ArchetypeList;
        Archetype* m_EmptyArchetype = nullptr;
        size_t m_AliveCount = 0;
        uint64_t m_StructuralVersion = 0;
        std::atomic<int> m_Iterating{0};
        std::atomic<int> m_Concurrent{0};
        mutable std::mutex m_DeferredMutex;
        std::vector<Entity> m_Deferred;
    };
}

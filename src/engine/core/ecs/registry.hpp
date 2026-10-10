#pragma once

#include <atomic>
#include <cassert>
#include <initializer_list>
#include <map>
#include <memory>
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

    /// Marker for the components a query must NOT have : registry.EachExcluding<Exclude<Dead>, Position>(...)
    template <typename... Ts> struct Exclude {};

    /// @brief The world of entities : creates and destroys them, attaches POD components, links them in a hierarchy, and runs
    /// queries over them.
    ///
    /// Storage : entities with the same set of components share an Archetype, which keeps each component in its own contiguous
    /// array. Adding or removing a component MOVES the entity to the archetype of its new set (a "structural change").
    ///
    /// Rules :
    ///  * Components are plain data : trivially copyable (memcpy-able, no destructor to run). Behaviour lives in systems.
    ///  * Structural changes (Create, Destroy, Add, Remove) are refused while a query is running (they return false / a null
    ///    entity) : use a CommandBuffer and Playback it afterwards. Changing a component VALUE, or the parent, is always allowed.
    ///  * Several threads may run read-only queries and edit the values of distinct entities at the same time ; any structural
    ///    change needs exclusive access.
    class Registry {
    public:
        Registry();
        ~Registry();
        Registry(const Registry&) = delete;
        Registry& operator=(const Registry&) = delete;

        // ------------------------------------------------------------------------------------------ entities

        /// A new entity with no component. Null entity if a query is running.
        Entity Create();

        /// A new entity with these components (one archetype lookup, no intermediate move)
        template <typename... Cs>
        Entity Create(const Cs&... values) {
            static_assert((std::is_trivially_copyable<Cs>::value && ...), "components must be trivially copyable (POD-like)");
            if (IsIterating()) return kNullEntity;
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

        /// Destroys the entity AND all its descendants. False if it is not alive or a query is running.
        bool Destroy(Entity e);

        bool IsAlive(Entity e) const;
        size_t Count() const { return m_AliveCount; }

        /// Bumped by every structural change : lets a cache know that what it holds may be stale
        uint64_t StructuralVersion() const { return m_StructuralVersion; }

        // ------------------------------------------------------------------------------------------ components

        template <typename T>
        bool Has(Entity e) const { return HasRaw(e, ComponentIdOf<T>()); }

        template <typename T>
        T* TryGet(Entity e) { return static_cast<T*>(GetRaw(e, ComponentIdOf<T>())); }
        template <typename T>
        const T* TryGet(Entity e) const { return static_cast<const T*>(GetRaw(e, ComponentIdOf<T>())); }

        /// The component, which the entity must have (checked in debug builds)
        template <typename T>
        T& Get(Entity e) { T* p = TryGet<T>(e); assert(p && "entity has no such component"); return *p; }
        template <typename T>
        const T& Get(Entity e) const { const T* p = TryGet<T>(e); assert(p && "entity has no such component"); return *p; }

        /// Adds T to the entity (or overwrites its value if it already has one). False if dead / a query is running
        /// (overwriting an existing value is allowed during a query).
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

        // ------------------------------------------------------------------------------------------ hierarchy

        /// Makes `parent` the parent of `child` (null parent : detach, the child becomes a root). Children keep the order in
        /// which they were attached. False if either is dead, or if it would make an entity its own ancestor.
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

        /// Visits every descendant, parents before their children (so a child can read its parent's already updated value)
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

        // ------------------------------------------------------------------------------------------ queries

        /// @brief Calls fn(Entity, Ts&...) (or fn(Ts&...)) for every entity that has all of Ts. Make a type const
        /// (`const Velocity`) to say you only read it : the same declaration is what the scheduler uses to order systems.
        template <typename... Ts, typename F>
        void Each(F&& fn) { EachExcluding<Exclude<>, Ts...>(std::forward<F>(fn)); }

        template <typename Excluded, typename... Ts, typename F>
        void EachExcluding(F&& fn) {
            IterationScope scope(*this);
            ForEachChunkImpl<Excluded, Ts...>([&fn](size_t n, Entity* entities, std::remove_cv_t<Ts>*... columns) {
                for (size_t i = 0; i < n; ++i) CallRow<F, Ts...>(fn, entities[i], columns[i]...);
            });
        }

        /// @brief Chunk-level access : fn(count, const Entity* entities, Ts*... columns). The tightest loop, vectorisable.
        template <typename... Ts, typename F>
        void EachChunk(F&& fn) {
            IterationScope scope(*this);
            ForEachChunkImpl<Exclude<>, Ts...>([&fn](size_t n, Entity* entities, std::remove_cv_t<Ts>*... columns) {
                fn(n, static_cast<const Entity*>(entities), columns...);
            });
        }

        /// @brief Like Each, with the chunks spread over the job system. fn runs on several threads : it may write the
        /// components it was handed (they belong to its entity alone) but nothing shared.
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

        /// Number of entities that have all of Ts
        template <typename... Ts>
        size_t CountOf() const {
            ComponentSet required{ComponentIdOf<std::remove_cv_t<Ts>>()...};
            Normalize(required);
            size_t total = 0;
            for (Archetype* archetype : Matching(required, ComponentSet{})) total += archetype->Count();
            return total;
        }

        size_t ArchetypeCount() const { return m_ArchetypeList.size(); }

        // ------------------------------------------------------------------------------------------ deferred changes

        struct PlaybackResult {
            size_t applied = 0;
            size_t skipped = 0;         // commands on an entity that was already dead
        };

        /// Applies the recorded changes, in order, and empties the buffer. Not allowed while a query runs (returns all skipped).
        PlaybackResult Playback(CommandBuffer& buffer);

        bool IsIterating() const { return m_Iterating.load(std::memory_order_acquire) > 0; }

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
        std::vector<Archetype*> m_ArchetypeList;                // creation order : iteration order is deterministic
        Archetype* m_EmptyArchetype = nullptr;
        size_t m_AliveCount = 0;
        uint64_t m_StructuralVersion = 0;
        std::atomic<int> m_Iterating{0};
    };
}

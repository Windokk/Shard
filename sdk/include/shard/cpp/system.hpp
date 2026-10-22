// Shard C++ SDK : systems and what they see of the entities.
//
//   ctx.Systems().Add("rotate", Shard::Phase::Update)
//       .Writes(spin)
//       .Run([spin](Shard::SystemCtx& s) {
//           s.Each(spin, [&](Spin& c) { c.angle += c.speed * s.DeltaTime(); });
//       });
//
// A system reads and writes only the types it declared (Reads / Writes) : the scheduler runs systems in parallel on that promise.
#pragma once

#include <array>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <shard/shard_ecs.h>
#include <shard/shard_events.h>
#include <shard/shard_experimental.h>
#include <shard/shard_systems.h>
#include <shard/cpp/component.hpp>
#include <shard/cpp/detail.hpp>
#include <shard/cpp/math.hpp>

namespace Shard {

    using Entity = ShardEntity;

    enum class Phase : uint32_t {
        Input = SHARD_PHASE_INPUT,
        PreSim = SHARD_PHASE_PRE_SIM,
        Fixed = SHARD_PHASE_FIXED,
        PostPhysics = SHARD_PHASE_POST_PHYSICS,
        Update = SHARD_PHASE_UPDATE,
        Late = SHARD_PHASE_LATE,
        Extract = SHARD_PHASE_EXTRACT,
    };

    /// The entities that have all of Cs (and none of the Without types), seen from one run of a system
    template <typename... Cs>
    class EntityQuery {
    public:
        EntityQuery(ShardEcs ecs, ComponentType<Cs>... types) : m_Ecs(ecs), m_With{ { types.handle... } } {}

        template <typename T>
        EntityQuery& Without(ComponentType<T> type) { m_Without.push_back(type.handle); return *this; }

        /// fn(Cs&...) or fn(Entity, Cs&...), once per entity
        template <typename F>
        void Each(F&& fn)
        {
            EachChunk([&fn](uint32_t count, const Entity* entities, Cs*... columns) {
                for (uint32_t i = 0; i < count; ++i) {
                    if constexpr (std::is_invocable<F&, Entity, Cs&...>::value) fn(entities[i], columns[i]...);
                    else fn(columns[i]...);
                }
            });
        }

        /// fn(count, const Entity* entities, Cs*... columns) : the tightest loop, once per chunk
        template <typename F>
        void EachChunk(F&& fn)
        {
            struct Call {
                F* fn;
                std::exception_ptr error;
            } call{ &fn, nullptr };
            auto trampoline = [](void* user, const ShardChunk* chunk) -> ShardResult {
                Call* c = static_cast<Call*>(user);
                try {
                    Invoke(*c->fn, chunk, std::index_sequence_for<Cs...>{});
                    return SHARD_OK;
                } catch (...) {
                    c->error = std::current_exception();     // thrown again once the host is out of the iteration
                    return SHARD_E_INTERNAL;
                }
            };
            ShardQuery query = Describe();
            const ShardResult result = Api().each_chunk(m_Ecs, &query, +trampoline, &call);
            if (call.error) std::rethrow_exception(call.error);
            detail::Check(result, "a query of a system");
        }

        uint64_t Count() const
        {
            ShardQuery query = Describe();
            uint64_t count = 0;
            detail::Check(Api().count(m_Ecs, &query, &count), "a count of a system");
            return count;
        }

    private:
        static const ShardEcsApi& Api() { return detail::Require<ShardEcsApi>(SHARD_IFACE_ECS); }

        template <typename F, size_t... Is>
        static void Invoke(F& fn, const ShardChunk* chunk, std::index_sequence<Is...>)
        {
            fn(chunk->count, chunk->entities, static_cast<Cs*>(chunk->columns[Is])...);
        }

        ShardQuery Describe() const
        {
            ShardQuery query{};
            query.structSize = sizeof(ShardQuery);
            query.withCount = uint32_t(sizeof...(Cs));
            query.with = m_With.data();
            query.withoutCount = uint32_t(m_Without.size());
            query.without = m_Without.data();
            return query;
        }

        ShardEcs m_Ecs;
        std::array<ShardComponentType, sizeof...(Cs)> m_With;
        std::vector<ShardComponentType> m_Without;
    };

    /// What one run of a system is given
    class SystemCtx {
    public:
        SystemCtx(ShardPluginCtx ctx, const ShardSystemCtx& run) : m_Ctx(ctx), m_Run(run) {}

        float DeltaTime() const { return m_Run.deltaTime; }
        float FixedDeltaTime() const { return m_Run.fixedDeltaTime; }
        int FixedStep() const { return m_Run.fixedStep; }
        int FixedStepCount() const { return m_Run.fixedStepCount; }
        uint64_t Frame() const { return m_Run.frame; }
        ShardEcs Ecs() const { return m_Run.ecs; }
        ShardPluginCtx Plugin() const { return m_Ctx; }

        template <typename... Cs>
        EntityQuery<Cs...> Query(ComponentType<Cs>... types) const { return EntityQuery<Cs...>(m_Run.ecs, types...); }

        /// fn(C&) or fn(Entity, C&) for every entity that has C
        template <typename C, typename F>
        void Each(ComponentType<C> type, F&& fn) const { Query(type).Each(std::forward<F>(fn)); }
        template <typename A, typename B, typename F>
        void Each(ComponentType<A> a, ComponentType<B> b, F&& fn) const { Query(a, b).Each(std::forward<F>(fn)); }

        bool IsAlive(Entity e) const { return Api().is_alive(m_Run.ecs, e) == SHARD_TRUE; }
        /// The entity's component, null if it has none. Valid until the end of the run.
        template <typename T>
        T* Get(Entity e, ComponentType<T> type) const
        {
            void* block = nullptr;
            return Api().get(m_Run.ecs, e, type.handle, &block) == SHARD_OK ? static_cast<T*>(block) : nullptr;
        }
        Entity Parent(Entity e) const
        {
            Entity parent = 0;
            Api().parent(m_Run.ecs, e, &parent);
            return parent;
        }

        // Structural changes : recorded, applied at the end of the phase
        Entity Spawn() const
        {
            Entity e = 0;
            detail::Check(Api().spawn(m_Run.ecs, &e), "spawn");
            return e;
        }
        template <typename T>
        void Add(Entity e, ComponentType<T> type, const T& value) const
        {
            static_assert(std::is_trivially_copyable<T>::value, "a component is plain data");
            detail::Check(Api().add(m_Run.ecs, e, type.handle, &value, sizeof(T)), "add");
        }
        template <typename T>
        void Remove(Entity e, ComponentType<T> type) const { detail::Check(Api().remove(m_Run.ecs, e, type.handle), "remove"); }
        void Destroy(Entity e) const { detail::Check(Api().destroy(m_Run.ecs, e), "destroy"); }
        void SetParent(Entity child, Entity parent) const { detail::Check(Api().set_parent(m_Run.ecs, child, parent), "set_parent"); }

        /// Moves the actor behind `e` (ABI 1.3) : its Transform takes the pose at the end of the phase. The system must declare the
        /// engine's shard.LocalTransform type in its writes. False if the entity has no actor. Throws Shard::Error in an engine older than 1.3.
        bool SetLocalTransform(Entity e, const Pose& pose) const
        {
            const ShardEcsApi* api = detail::Interface<ShardEcsApi>(SHARD_IFACE_ECS, 1);
            if (!api) throw Error(SHARD_E_UNSUPPORTED, "set_local_transform needs shard.ecs 1.1");
            ShardLocalTransform t{};
            std::memcpy(&t, &pose, sizeof(t));
            const ShardResult r = api->set_local_transform(m_Run.ecs, e, &t);
            if (r == SHARD_E_NOT_FOUND) return false;
            detail::Check(r, "set_local_transform");
            return true;
        }

        /// The text of a string field of a component, from inside a system (see Components::SetText / GetText)
        void SetText(Text& field, std::string_view text) const { Components(m_Ctx).SetText(field, text); }
        std::string GetText(Text field) const { return Components(m_Ctx).GetText(field); }

        /// A named event, delivered at the end of the frame (shard_events.h)
        void Emit(const std::string& name, const void* payload, size_t size) const
        {
            if (const auto* events = detail::Interface<ShardEventsApi>(SHARD_IFACE_EVENTS)) events->emit(m_Ctx, Str(name), payload, uint64_t(size));
        }
        template <typename T>
        void Emit(const std::string& name, const T& payload) const
        {
            static_assert(std::is_trivially_copyable<T>::value, "an event's payload is copied as bytes");
            Emit(name, &payload, sizeof(T));
        }

    private:
        static const ShardEcsApi& Api() { return detail::Require<ShardEcsApi>(SHARD_IFACE_ECS); }
        ShardPluginCtx m_Ctx;
        ShardSystemCtx m_Run;
    };

    using SystemFn = std::function<void(SystemCtx&)>;

    /// Built by Systems::Add ; Run registers it
    class SystemBuilder {
    public:
        SystemBuilder(ShardPluginCtx ctx, std::string name, Phase phase) : m_Ctx(ctx), m_Name(std::move(name)), m_Phase(phase) {}

        template <typename... Ts>
        SystemBuilder& Reads(ComponentType<Ts>... types) { (m_Reads.push_back(types.handle), ...); return *this; }
        template <typename... Ts>
        SystemBuilder& Writes(ComponentType<Ts>... types) { (m_Writes.push_back(types.handle), ...); return *this; }
        SystemBuilder& Exclusive() { m_Flags |= SHARD_SYSTEM_EXCLUSIVE; return *this; }
        SystemBuilder& MainThread() { m_Flags |= SHARD_SYSTEM_MAIN_THREAD; return *this; }
        /// A full name ("com.acme.weather/wind"), or "./x" for this plugin's "x"
        SystemBuilder& After(std::string other) { m_After.push_back(std::move(other)); return *this; }
        SystemBuilder& Before(std::string other) { m_Before.push_back(std::move(other)); return *this; }

        /// Adds the system. Throws Shard::Error if the host refuses it.
        void Run(SystemFn fn)
        {
            SystemFn* kept = detail::Keep(std::move(fn));
            std::vector<ShardStr> after, before;
            for (const std::string& s : m_After) after.push_back(Str(s));
            for (const std::string& s : m_Before) before.push_back(Str(s));
            ShardSystemDesc desc{};
            desc.structSize = sizeof(ShardSystemDesc);
            desc.phase = uint32_t(m_Phase);
            desc.flags = m_Flags;
            desc.readCount = uint32_t(m_Reads.size());
            desc.reads = m_Reads.data();
            desc.writeCount = uint32_t(m_Writes.size());
            desc.writes = m_Writes.data();
            desc.afterCount = uint32_t(after.size());
            desc.after = after.data();
            desc.beforeCount = uint32_t(before.size());
            desc.before = before.data();
            desc.name = Str(m_Name);
            desc.run = &Trampoline;
            desc.user = kept;
            const auto& api = detail::Require<ShardSystemsApi>(SHARD_IFACE_SYSTEMS);
            detail::Check(api.add(m_Ctx, &desc), "system '" + m_Name + "'");
        }

    private:
        static ShardResult SHARD_CALL Trampoline(void* user, ShardPluginCtx ctx, const ShardSystemCtx* run)
        {
            return detail::Guard(ctx, "a system", [&] {
                SystemCtx s(ctx, *run);
                (*static_cast<SystemFn*>(user))(s);
            });
        }

        ShardPluginCtx m_Ctx;
        std::string m_Name;
        Phase m_Phase;
        uint32_t m_Flags = 0;
        std::vector<ShardComponentType> m_Reads, m_Writes;
        std::vector<std::string> m_After, m_Before;
    };

    namespace experimental {

        /// @brief The engine's registry from the main thread, outside the systems : shard.experimental.world_ecs 0.1, with NO compatibility
        /// promise (shard_experimental.h). Every function of the SystemCtx it gives works, on any type ; the structural changes are applied
        /// when it goes. Refused to a native plugin signed by a trusted publisher. Use it inside one callback.
        class World {
        public:
            explicit World(ShardPluginCtx ctx) : m_Ctx(ctx)
            {
                const void* table = nullptr;
                if (!detail::Host() || detail::Host()->query_interface(Str(SHARD_IFACE_EXPERIMENTAL_WORLD_ECS), 0, 1, &table) != SHARD_OK)
                    throw Error(SHARD_E_UNSUPPORTED, "this engine has no shard.experimental.world_ecs 0.1");
                m_Api = static_cast<const ShardWorldEcsApi*>(table);
                ShardSystemCtx run{};
                run.structSize = sizeof(run);
                detail::Check(m_Api->open(ctx, &run.ecs), "shard.experimental.world_ecs");
                m_System = std::make_unique<SystemCtx>(ctx, run);
            }
            ~World() { if (m_System) m_Api->close(m_Ctx, m_System->Ecs()); }
            World(const World&) = delete;
            World& operator=(const World&) = delete;

            SystemCtx& Ecs() { return *m_System; }

        private:
            ShardPluginCtx m_Ctx;
            const ShardWorldEcsApi* m_Api = nullptr;
            std::unique_ptr<SystemCtx> m_System;
        };
    }

    class Systems {
    public:
        explicit Systems(ShardPluginCtx ctx) : m_Ctx(ctx) {}
        /// "<plugin id>/<name>" once Run is called on what this returns
        SystemBuilder Add(std::string name, Phase phase) const { return SystemBuilder(m_Ctx, std::move(name), phase); }
        bool SetEnabled(const std::string& name, bool enabled) const { return Api().set_enabled(m_Ctx, Str(name), enabled ? 1 : 0) == SHARD_OK; }
        bool Remove(const std::string& name) const { return Api().remove(m_Ctx, Str(name)) == SHARD_OK; }

    private:
        static const ShardSystemsApi& Api() { return detail::Require<ShardSystemsApi>(SHARD_IFACE_SYSTEMS); }
        ShardPluginCtx m_Ctx;
    };
}

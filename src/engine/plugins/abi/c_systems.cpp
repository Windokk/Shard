#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <shard/shard_ecs.h>
#include <shard/shard_experimental.h>
#include <shard/shard_systems.h>

#include "engine/core/ecs/registry.hpp"
#include "engine/core/ecs/scheduler.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/components/transform.hpp"
#include "engine/world/ecs_components.hpp"
#include "engine/world/engine.hpp"

// shard.systems and shard.ecs : a C system is a SystemDesc whose function calls the plugin. While it runs, the ShardEcs it was handed
// designates that run on that thread (a thread_local), so a handle kept past the run, or used from another thread, finds nothing.

namespace Shard::Engine::Plugins::CAbi {

    struct CSystem {
        std::string name;                       // full
        PluginOwner owner = 0;
        ShardSystemFn run = nullptr;
        void* user = nullptr;
        Core::Ecs::ComponentSet access;         // reads and writes : what its queries may touch
        bool exclusive = false;
    };

    namespace {
        thread_local SystemRun* tRun = nullptr;
        std::atomic<uint64_t> gNextRun{ 0 };

        // The poses systems set, by system name : applied in that order at the end of the phase, whatever order the systems finished in
        std::mutex gPoseMutex;
        std::map<std::string, std::vector<QueuedPose>> gPoses;

        SystemRun* RunOf(ShardEcs ecs)
        {
            SystemRun* run = tRun;
            return run && ecs != 0 && run->handle == ecs ? run : nullptr;
        }

        Core::Ecs::Entity ToEntity(ShardEntity e) { return Core::Ecs::Entity(e); }

        bool IdOf(ShardComponentType type, Core::Ecs::ComponentId& out)
        {
            if (type == 0 || type > uint64_t(Core::Ecs::ComponentRegistry::Global().Count())) return false;
            out = Core::Ecs::ComponentId(type - 1);
            return true;
        }

        // The system declared it (or may touch anything)
        bool MayTouch(const SystemRun& run, Core::Ecs::ComponentId id)
        {
            return !run.system || run.system->exclusive || Core::Ecs::Contains(run.system->access, id);
        }

        // The ids of a query, checked against what the system declared
        ShardResult ReadQuery(const SystemRun& run, const ShardQuery* query, std::vector<Core::Ecs::ComponentId>& with, Core::Ecs::ComponentSet& without)
        {
            if (!query || query->structSize < sizeof(ShardQuery)) return SHARD_E_INVALID_ARG;
            if ((query->withCount && !query->with) || (query->withoutCount && !query->without)) return SHARD_E_INVALID_ARG;
            for (uint32_t i = 0; i < query->withCount; ++i) {
                Core::Ecs::ComponentId id;
                if (!IdOf(query->with[i], id) || !MayTouch(run, id)) return SHARD_E_INVALID_ARG;
                if (std::find(with.begin(), with.end(), id) != with.end()) return SHARD_E_INVALID_ARG;
                with.push_back(id);
            }
            for (uint32_t i = 0; i < query->withoutCount; ++i) {
                Core::Ecs::ComponentId id;
                if (!IdOf(query->without[i], id)) return SHARD_E_INVALID_ARG;
                without.push_back(id);
            }
            return SHARD_OK;
        }

        // ---- shard.ecs ----

        ShardResult SHARD_CALL EachChunk(ShardEcs ecs, const ShardQuery* query, ShardChunkFn fn, void* user)
        {
            try {
                SystemRun* run = RunOf(ecs);
                if (!run) return SHARD_E_STALE_HANDLE;
                if (!fn) return SHARD_E_INVALID_ARG;
                std::vector<Core::Ecs::ComponentId> with;
                Core::Ecs::ComponentSet without;
                const ShardResult valid = ReadQuery(*run, query, with, without);
                if (valid != SHARD_OK) return valid;
                ShardResult result = SHARD_OK;
                run->context->registry.EachChunkRaw(with, without, [&](size_t count, const Core::Ecs::Entity* entities, void* const* columns) {
                    ShardChunk chunk;
                    chunk.structSize = sizeof(ShardChunk);
                    chunk.count = uint32_t(count);
                    chunk.entities = reinterpret_cast<const ShardEntity*>(entities);
                    chunk.columns = columns;
                    result = fn(user, &chunk);
                    return result == SHARD_OK;
                });
                return result;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Count(ShardEcs ecs, const ShardQuery* query, uint64_t* out)
        {
            try {
                SystemRun* run = RunOf(ecs);
                if (!run) return SHARD_E_STALE_HANDLE;
                if (!out) return SHARD_E_INVALID_ARG;
                std::vector<Core::Ecs::ComponentId> with;
                Core::Ecs::ComponentSet without;
                const ShardResult valid = ReadQuery(*run, query, with, without);
                if (valid != SHARD_OK) return valid;
                *out = run->context->registry.CountRaw(with, without);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        int32_t SHARD_CALL IsAlive(ShardEcs ecs, ShardEntity entity)
        {
            try {
                SystemRun* run = RunOf(ecs);
                return run && run->context->registry.IsAlive(ToEntity(entity)) ? SHARD_TRUE : SHARD_FALSE;
            } catch (...) { return SHARD_FALSE; }
        }

        ShardResult SHARD_CALL Get(ShardEcs ecs, ShardEntity entity, ShardComponentType type, void** out)
        {
            try {
                SystemRun* run = RunOf(ecs);
                if (!run) return SHARD_E_STALE_HANDLE;
                if (!out) return SHARD_E_INVALID_ARG;
                *out = nullptr;
                Core::Ecs::ComponentId id;
                if (!IdOf(type, id) || !MayTouch(*run, id)) return SHARD_E_INVALID_ARG;
                *out = run->context->registry.GetRaw(ToEntity(entity), id);
                return *out ? SHARD_OK : SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Parent(ShardEcs ecs, ShardEntity entity, ShardEntity* out)
        {
            try {
                SystemRun* run = RunOf(ecs);
                if (!run) return SHARD_E_STALE_HANDLE;
                if (!out) return SHARD_E_INVALID_ARG;
                if (!run->context->registry.IsAlive(ToEntity(entity))) return SHARD_E_NOT_FOUND;
                *out = run->context->registry.GetParent(ToEntity(entity)).value;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Spawn(ShardEcs ecs, ShardEntity* placeholder)
        {
            try {
                SystemRun* run = RunOf(ecs);
                if (!run) return SHARD_E_STALE_HANDLE;
                if (!placeholder) return SHARD_E_INVALID_ARG;
                *placeholder = run->context->commands.Create().value;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Destroy(ShardEcs ecs, ShardEntity entity)
        {
            try {
                SystemRun* run = RunOf(ecs);
                if (!run) return SHARD_E_STALE_HANDLE;
                if (entity == 0) return SHARD_E_INVALID_ARG;
                run->context->commands.Destroy(ToEntity(entity));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Add(ShardEcs ecs, ShardEntity entity, ShardComponentType type, const void* data, uint64_t size)
        {
            try {
                SystemRun* run = RunOf(ecs);
                if (!run) return SHARD_E_STALE_HANDLE;
                Core::Ecs::ComponentId id;
                if (entity == 0 || !data || !IdOf(type, id)) return SHARD_E_INVALID_ARG;
                if (Core::Ecs::ComponentRegistry::Global().Info(id).size != size) return SHARD_E_INVALID_ARG;
                run->context->commands.AddRaw(ToEntity(entity), id, data, size_t(size));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Remove(ShardEcs ecs, ShardEntity entity, ShardComponentType type)
        {
            try {
                SystemRun* run = RunOf(ecs);
                if (!run) return SHARD_E_STALE_HANDLE;
                Core::Ecs::ComponentId id;
                if (entity == 0 || !IdOf(type, id)) return SHARD_E_INVALID_ARG;
                run->context->commands.RemoveRaw(ToEntity(entity), id);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetParent(ShardEcs ecs, ShardEntity child, ShardEntity parent)
        {
            try {
                SystemRun* run = RunOf(ecs);
                if (!run) return SHARD_E_STALE_HANDLE;
                if (child == 0) return SHARD_E_INVALID_ARG;
                run->context->commands.SetParent(ToEntity(child), ToEntity(parent));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        bool Finite(const float* v, int n)
        {
            for (int i = 0; i < n; ++i) if (!std::isfinite(v[i])) return false;
            return true;
        }

        ShardResult SHARD_CALL SetLocalTransform(ShardEcs ecs, ShardEntity entity, const ShardLocalTransform* transform)
        {
            try {
                SystemRun* run = RunOf(ecs);
                if (!run) return SHARD_E_STALE_HANDLE;
                if (entity == 0 || !transform) return SHARD_E_INVALID_ARG;
                if (!MayTouch(*run, Core::Ecs::ComponentIdOf<Worlds::LocalTransform>())) return SHARD_E_INVALID_ARG;
                if (!Finite(transform->position, 3) || !Finite(transform->rotation, 4) || !Finite(transform->scale, 3)) return SHARD_E_INVALID_ARG;
                const float lengthSquared = transform->rotation[0] * transform->rotation[0] + transform->rotation[1] * transform->rotation[1] +
                                            transform->rotation[2] * transform->rotation[2] + transform->rotation[3] * transform->rotation[3];
                if (lengthSquared < 1e-12f) return SHARD_E_INVALID_ARG;
                if (!run->context->registry.GetRaw(ToEntity(entity), Core::Ecs::ComponentIdOf<Worlds::ActorLink>())) return SHARD_E_NOT_FOUND;
                QueuedPose pose;
                pose.entity = entity;
                pose.pose = *transform;
                run->poses.push_back(pose);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // ---- shard.systems ----

        bool IsSystemName(const std::string& name)
        {
            return !name.empty() && name.size() <= 128 && name.find('/') == std::string::npos;
        }

        ShardResult ReadIds(const ShardComponentType* types, uint32_t count, Core::Ecs::ComponentSet& out)
        {
            if (count && !types) return SHARD_E_INVALID_ARG;
            for (uint32_t i = 0; i < count; ++i) {
                Core::Ecs::ComponentId id;
                if (!IdOf(types[i], id)) return SHARD_E_INVALID_ARG;
                out.push_back(id);
            }
            Core::Ecs::Normalize(out);
            return SHARD_OK;
        }

        ShardResult ReadNames(const ShardStr* names, uint32_t count, std::vector<std::string>& out)
        {
            if (count && !names) return SHARD_E_INVALID_ARG;
            for (uint32_t i = 0; i < count; ++i) out.push_back(ToString(names[i]));
            return SHARD_OK;
        }

        ShardResult SHARD_CALL SystemAdd(ShardPluginCtx handle, const ShardSystemDesc* desc)
        {
            try {
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                if (!desc || desc->structSize < sizeof(ShardSystemDesc) || !desc->run) return SHARD_E_INVALID_ARG;
                if (desc->phase >= uint32_t(Core::Ecs::Phase::Count)) return SHARD_E_INVALID_ARG;
                const std::string local = ToString(desc->name);
                if (!IsSystemName(local)) return SHARD_E_INVALID_ARG;
                if (ctx->Systems().Has(local)) return SHARD_E_ALREADY_EXISTS;

                Core::Ecs::SystemDesc system;
                system.name = local;
                system.phase = Core::Ecs::Phase(desc->phase);
                system.exclusive = (desc->flags & SHARD_SYSTEM_EXCLUSIVE) != 0;
                system.mainThread = (desc->flags & SHARD_SYSTEM_MAIN_THREAD) != 0;
                if (ReadIds(desc->reads, desc->readCount, system.reads) != SHARD_OK) return SHARD_E_INVALID_ARG;
                if (ReadIds(desc->writes, desc->writeCount, system.writes) != SHARD_OK) return SHARD_E_INVALID_ARG;
                if (ReadNames(desc->after, desc->afterCount, system.after) != SHARD_OK) return SHARD_E_INVALID_ARG;
                if (ReadNames(desc->before, desc->beforeCount, system.before) != SHARD_OK) return SHARD_E_INVALID_ARG;

                auto info = std::make_shared<CSystem>();
                info->name = ctx->Systems().NameOf(local);
                info->owner = PluginOwner(handle);
                info->run = desc->run;
                info->user = desc->user;
                info->exclusive = system.exclusive;
                info->access = system.reads;
                info->access.insert(info->access.end(), system.writes.begin(), system.writes.end());
                Core::Ecs::Normalize(info->access);

                // The registrar wraps this : it names the plugin around the call, catches what is thrown and counts it as an error of the plugin
                system.run = [info](Core::Ecs::SystemContext& sc) {
                    SystemRun run;
                    run.handle = ++gNextRun;
                    run.context = &sc;
                    run.system = info.get();
                    ShardSystemCtx c;
                    c.structSize = sizeof(ShardSystemCtx);
                    c.deltaTime = sc.deltaTime;
                    c.fixedDeltaTime = sc.fixedDeltaTime;
                    c.fixedStep = sc.fixedStep;
                    c.fixedStepCount = sc.fixedStepCount;
                    c.frame = sc.frame;
                    c.ecs = run.handle;
                    SystemRun* previous = tRun;
                    tRun = &run;
                    ShardResult result = SHARD_E_INTERNAL;
                    try { result = info->run(info->user, info->owner, &c); } catch (...) { tRun = previous; throw; }
                    tRun = previous;
                    if (!run.events.empty()) QueueEvents(info->name, run.events);
                    if (!run.poses.empty()) QueuePoses(info->name, run.poses);
                    if (result != SHARD_OK) throw std::runtime_error("system '" + info->name + "' returned " + std::to_string(result));
                };
                if (ctx->Systems().Add(std::move(system)).empty()) return SHARD_E_ALREADY_EXISTS;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SystemSetEnabled(ShardPluginCtx handle, ShardStr name, int32_t enabled)
        {
            try {
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                const std::string local = ToString(name);
                if (!IsSystemName(local)) return SHARD_E_INVALID_ARG;
                return ctx->Systems().SetEnabled(local, enabled != 0) ? SHARD_OK : SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SystemRemove(ShardPluginCtx handle, ShardStr name)
        {
            try {
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                const std::string local = ToString(name);
                if (!IsSystemName(local)) return SHARD_E_INVALID_ARG;
                return ctx->Systems().Remove(local) ? SHARD_OK : SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    // ---- shard.experimental.world_ecs : a run of no system, opened on the main thread ----

    namespace {
        struct MainRun {
            explicit MainRun(Core::Ecs::Registry& registry) : context{ registry, commands, nullptr, 0.0f, 0.0f, 0, 0, 0 } {}
            Core::IEngineContext* engine = nullptr;
            SystemRun run;
            Core::Ecs::CommandBuffer commands;
            Core::Ecs::SystemContext context;
            SystemRun* previous = nullptr;
            PluginOwner owner = 0;
        };
        // Main thread only : the last opened last
        std::vector<std::unique_ptr<MainRun>> gMainRuns;

        void CloseTop()
        {
            std::unique_ptr<MainRun> top = std::move(gMainRuns.back());
            gMainRuns.pop_back();
            tRun = top->previous;
            top->context.registry.Playback(top->commands);
            ApplyPoses(top->engine, top->run.poses);
        }

        ShardResult SHARD_CALL WorldEcsOpen(ShardPluginCtx handle, ShardEcs* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!ExperimentalAllowed(handle)) return SHARD_E_UNSUPPORTED;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                Core::IEngineContext* engine = ctx->Engine();
                Core::Ecs::Registry* registry = engine ? engine->GetEcs() : nullptr;
                if (!registry) return SHARD_E_UNSUPPORTED;
                if (!registry->CanChangeStructure()) return SHARD_E_WRONG_THREAD;      // a phase runs
                auto main = std::make_unique<MainRun>(*registry);
                main->engine = engine;
                main->run.handle = ++gNextRun;
                main->run.context = &main->context;
                main->run.mainThread = true;
                main->owner = PluginOwner(handle);
                main->previous = tRun;
                tRun = &main->run;
                *out = main->run.handle;
                gMainRuns.push_back(std::move(main));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL WorldEcsClose(ShardPluginCtx handle, ShardEcs ecs)
        {
            try {
                if (gMainRuns.empty() || gMainRuns.back()->run.handle != ecs || gMainRuns.back()->owner != PluginOwner(handle)) return SHARD_E_STALE_HANDLE;
                CloseTop();
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    void CloseWorldEcs(PluginOwner owner)
    {
        // What a plugin left open : closed, and those opened after it with it (they are nested in it)
        for (size_t i = 0; i < gMainRuns.size(); ++i) {
            if (gMainRuns[i]->owner != owner) continue;
            while (gMainRuns.size() > i) CloseTop();
            break;
        }
    }

    const void* WorldEcsApi()
    {
        static const ShardWorldEcsApi api = { sizeof(ShardWorldEcsApi), &WorldEcsOpen, &WorldEcsClose };
        return &api;
    }

    SystemRun* CurrentRun() { return tRun; }

    void QueuePoses(const std::string& systemName, std::vector<QueuedPose>& poses)
    {
        std::lock_guard<std::mutex> lock(gPoseMutex);
        auto& queue = gPoses[systemName];
        queue.insert(queue.end(), poses.begin(), poses.end());
        poses.clear();
    }

    void ApplyPoses(Core::IEngineContext* engine, const std::vector<QueuedPose>& poses)
    {
        if (poses.empty() || !engine) return;
        Core::Ecs::Registry* ecs = engine->GetEcs();
        Core::ObjectIDManager* objects = engine->GetObjectIDManager();
        if (!ecs || !objects) return;
        for (const QueuedPose& queued : poses) {
            const Core::Ecs::Entity entity = Core::Ecs::Entity(queued.entity);
            if (!ecs->IsAlive(entity)) continue;
            const Worlds::ActorLink* link = ecs->TryGet<Worlds::ActorLink>(entity);
            if (!link) continue;
            auto actor = std::dynamic_pointer_cast<Objects::Actor>(objects->GetObjectFromID(Core::ObjectID(link->objectId)));
            if (!actor || !actor->transform) continue;
            Objects::Components::Transform& transform = *actor->transform;
            const ShardLocalTransform& pose = queued.pose;
            const glm::vec3 position(pose.position[0], pose.position[1], pose.position[2]);
            const glm::quat rotation = glm::normalize(glm::quat(pose.rotation[3], pose.rotation[0], pose.rotation[1], pose.rotation[2]));
            const glm::vec3 scale(pose.scale[0], pose.scale[1], pose.scale[2]);
            // Only what changed : a system that sets the same pose every frame costs the actor nothing
            if (transform.position != position) transform.SetPosition(position);
            if (transform.rotation != rotation) transform.SetRotation(rotation);
            if (transform.scale != scale) transform.SetScale(scale);
        }
    }


    const void* EcsApi()
    {
        static const ShardEcsApi api = { sizeof(ShardEcsApi), &EachChunk, &Count, &IsAlive, &Get, &Parent, &Spawn, &Destroy, &Add, &Remove, &SetParent, &SetLocalTransform };
        return &api;
    }

    const void* SystemsApi()
    {
        static const ShardSystemsApi api = { sizeof(ShardSystemsApi), &SystemAdd, &SystemSetEnabled, &SystemRemove };
        return &api;
    }
}

namespace Shard::Engine::Plugins {

    void FlushCPoses(Core::IEngineContext* engine)
    {
        std::map<std::string, std::vector<CAbi::QueuedPose>> taken;
        {
            std::lock_guard<std::mutex> lock(CAbi::gPoseMutex);
            if (CAbi::gPoses.empty()) return;
            taken.swap(CAbi::gPoses);
        }
        for (const auto& kv : taken) CAbi::ApplyPoses(engine, kv.second);
    }
}

#include <nlohmann/json.hpp>

#include <shard/shard_worlds.h>

#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/world/prefab.hpp"

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        Worlds::WorldManager* Manager()
        {
            Core::IEngineContext* engine = Core::gSharedEnginePtr;
            return engine ? engine->GetWorldManager() : nullptr;
        }

        ShardResult SHARD_CALL LoadWorld(ShardStr path)
        {
            try {
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                Worlds::WorldManager* worlds = Manager();
                Core::IEngineContext* engine = Core::gSharedEnginePtr;
                if (!worlds || !engine || !engine->GetAssetIDManager()) return SHARD_E_UNSUPPORTED;
                const std::string name = ToString(path);
                if (name.empty()) return SHARD_E_INVALID_ARG;
                Filesystem::AssetIDManager* assets = engine->GetAssetIDManager();
                if (!assets->GetAssetFromID(assets->GetIDFromNameInProject(name))) return SHARD_E_NOT_FOUND;
                if (worlds->IsAsyncLoadInProgress()) return SHARD_E_ALREADY_EXISTS;
                worlds->LoadWorldAsync(name);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        int32_t SHARD_CALL IsLoading()
        {
            try {
                Worlds::WorldManager* worlds = Manager();
                return worlds && worlds->IsAsyncLoadInProgress() ? SHARD_TRUE : SHARD_FALSE;
            } catch (...) { return SHARD_FALSE; }
        }

        int32_t SHARD_CALL LoadedCount()
        {
            try {
                Worlds::WorldManager* worlds = Manager();
                return worlds ? int32_t(worlds->GetLoadedWorldCount()) : 0;
            } catch (...) { return 0; }
        }

        bool ReadPrefab(const std::string& name, std::string& text, std::string& canonical)
        {
            ContentMounts& content = PluginManager::Get().Content();
            if (ContentMounts::IsModUri(name)) {
                const auto read = content.ReadText(name);
                if (!read) return false;
                text = *read;
                canonical = name;
                return true;
            }
            const std::string logical = name.find('/') == std::string::npos ? "prefabs/" + name + ".json" : name;
            const auto found = content.ResolveLogical(logical);
            if (!found) return false;
            const auto read = content.ReadText(found->uri);
            if (!read) return false;
            text = *read;
            canonical = found->uri;
            return true;
        }

        [[maybe_unused]] const bool kReaderRegistered = (Worlds::SetPrefabContentReader(&ReadPrefab), true);

        ShardResult WorldOfAnchor(ShardActor anchor, Worlds::World*& world)
        {
            std::shared_ptr<Objects::Actor> at = ActorOf(anchor);
            if (!at) return SHARD_E_STALE_HANDLE;
            if (!at->world) return SHARD_E_NOT_FOUND;
            world = at->world;
            return SHARD_OK;
        }

        ShardResult SpawnWith(ShardActor anchor, ShardStr prefab, ShardActor parent, uint32_t flags, const ShardLocalTransform* transform,
                              ShardStr name, ShardStr overridesJson, ShardActor* out)
        {
            if (!out) return SHARD_E_INVALID_ARG;
            *out = 0;
            if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
            Worlds::World* world = nullptr;
            const ShardResult found = WorldOfAnchor(anchor, world);
            if (found != SHARD_OK) return found;

            std::shared_ptr<Objects::Actor> parentActor;
            if (parent) {
                parentActor = ActorOf(parent);
                if (!parentActor) return SHARD_E_STALE_HANDLE;
            }

            nlohmann::json overrides = nlohmann::json::object();
            if (overridesJson.size > 0) {
                overrides = nlohmann::json::parse(ToString(overridesJson), nullptr, false);
                if (overrides.is_discarded() || !overrides.is_object()) return SHARD_E_INVALID_ARG;
            }
            if (flags & SHARD_PREFAB_SET_TRANSFORM) {
                if (!transform) return SHARD_E_INVALID_ARG;
                const nlohmann::json pose = Worlds::MakeTransformOverride(transform->position, transform->rotation, transform->scale);
                overrides["components"]["transform"].merge_patch(pose["components"]["transform"]);
            }

            const std::string prefabName = ToString(prefab);
            if (prefabName.empty()) return SHARD_E_INVALID_ARG;
            nlohmann::json probe;
            std::string canonical;
            if (!Worlds::LoadResolvedPrefab(prefabName, probe, canonical)) return SHARD_E_NOT_FOUND;

            auto spawned = world->SpawnPrefab(prefabName, parentActor, overrides, ToString(name));
            if (!spawned) return SHARD_E_INVALID_ARG;
            *out = ActorHandle(spawned);
            return SHARD_OK;
        }

        ShardResult SHARD_CALL SpawnPrefab(ShardActor anchor, ShardStr prefab, ShardActor parent, ShardActor* out)
        {
            try { return SpawnWith(anchor, prefab, parent, 0, nullptr, ShardStr{ nullptr, 0 }, ShardStr{ nullptr, 0 }, out); }
            catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SpawnPrefabEx(ShardActor anchor, ShardStr prefab, ShardActor parent, uint32_t flags, const ShardLocalTransform* transform,
                                             ShardStr name, ShardStr overridesJson, ShardActor* out)
        {
            try { return SpawnWith(anchor, prefab, parent, flags, transform, name, overridesJson, out); }
            catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL FindPrefab(ShardActor anchor, ShardStr prefab, ShardActor* out, uint32_t capacity, uint32_t* count)
        {
            try {
                if (!count) return SHARD_E_INVALID_ARG;
                *count = 0;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                Worlds::World* world = nullptr;
                const ShardResult found = WorldOfAnchor(anchor, world);
                if (found != SHARD_OK) return found;
                const auto instances = world->FindPrefabInstances(ToString(prefab));
                *count = uint32_t(instances.size());
                if (out)
                    for (uint32_t i = 0; i < capacity && i < instances.size(); ++i)
                        out[i] = ActorHandle(instances[i]);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL GetPrefab(ShardActor actor, char* name, uint64_t capacity, uint64_t* size, ShardActor* root)
        {
            try {
                if (root) *root = 0;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                std::shared_ptr<Objects::Actor> a = ActorOf(actor);
                if (!a) return SHARD_E_STALE_HANDLE;
                std::shared_ptr<Objects::Actor> top = a->GetPrefabRoot();
                if (!top) return SHARD_E_NOT_FOUND;
                if (name || size) {
                    uint64_t length = 0;
                    const ShardResult copied = CopyOut(top->GetPrefab()->name, name, capacity, size ? size : &length);
                    if (copied != SHARD_OK) return copied;
                }
                if (root) *root = ActorHandle(top);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL DestroyPrefab(ShardActor actor)
        {
            try {
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                std::shared_ptr<Objects::Actor> a = ActorOf(actor);
                if (!a) return SHARD_E_STALE_HANDLE;
                std::shared_ptr<Objects::Actor> top = a->GetPrefabRoot();
                if (!top) return SHARD_E_NOT_FOUND;
                top->Destroy();
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL DestroyPrefabs(ShardActor anchor, ShardStr prefab, uint32_t* count)
        {
            try {
                if (count) *count = 0;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                Worlds::World* world = nullptr;
                const ShardResult found = WorldOfAnchor(anchor, world);
                if (found != SHARD_OK) return found;
                const size_t destroyed = world->DestroyPrefabInstances(ToString(prefab));
                if (count) *count = uint32_t(destroyed);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    namespace {
        ShardResult SHARD_CALL WorldAt(int32_t index, int32_t* assetId, char* name, uint64_t capacity, uint64_t* size, ShardActor* anchor)
        {
            try {
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                Worlds::WorldManager* worlds = Manager();
                if (!worlds) return SHARD_E_UNSUPPORTED;
                if (index < 0 || index >= int32_t(worlds->GetLoadedWorldCount())) return SHARD_E_NOT_FOUND;
                Worlds::World* world = worlds->GetWorldAt(index);
                if (!world) return SHARD_E_NOT_FOUND;
                if (assetId) *assetId = world->ResolveAssetID();
                if (name || size) {
                    uint64_t length = 0;
                    const ShardResult copied = CopyOut(world->GetName(), name, capacity, size ? size : &length);
                    if (copied != SHARD_OK) return copied;
                }
                if (anchor) {
                    *anchor = 0;
                    auto roots = world->GetRootActors();
                    Core::ObjectID best;
                    bool found = false;
                    for (const auto& [id, actor] : roots) {
                        if (!actor) continue;
                        if (!found || id.GetAsInt() < best.GetAsInt()) { best = id; found = true; }
                    }
                    if (found) *anchor = ActorHandle(roots[best]);
                }
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    const void* WorldsApi()
    {
        static const ShardWorldsApi api = { sizeof(ShardWorldsApi), &LoadWorld, &IsLoading, &LoadedCount, &SpawnPrefab, &WorldAt, &SpawnPrefabEx, &FindPrefab, &GetPrefab, &DestroyPrefab, &DestroyPrefabs };
        return &api;
    }
}

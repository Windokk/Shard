#include <nlohmann/json.hpp>

#include <shard/shard_worlds.h>

#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"

// shard.worlds : asking the engine to load a world, and building a module's prefabs

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
                worlds->LoadWorldAsync(name);       // the engine swaps the worlds in at the start of a later frame
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

        // The text of a prefab : a mod:// address, a logical path, or a bare name
        bool ReadPrefab(const std::string& name, std::string& text)
        {
            ContentMounts& content = PluginManager::Get().Content();
            if (ContentMounts::IsModUri(name)) {
                const auto read = content.ReadText(name);
                if (!read) return false;
                text = *read;
                return true;
            }
            const std::string logical = name.find('/') == std::string::npos ? "prefabs/" + name + ".json" : name;
            const auto found = content.ResolveLogical(logical);
            if (!found) return false;
            const auto read = content.ReadText(found->uri);
            if (!read) return false;
            text = *read;
            return true;
        }

        ShardResult SHARD_CALL SpawnPrefab(ShardActor anchor, ShardStr prefab, ShardActor parent, ShardActor* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                std::shared_ptr<Objects::Actor> at = ActorOf(anchor);
                if (!at) return SHARD_E_STALE_HANDLE;
                if (!at->world) return SHARD_E_NOT_FOUND;
                std::shared_ptr<Objects::Actor> parentActor;
                if (parent) {
                    parentActor = ActorOf(parent);
                    if (!parentActor) return SHARD_E_STALE_HANDLE;
                }
                std::string text;
                if (!ReadPrefab(ToString(prefab), text)) return SHARD_E_NOT_FOUND;
                const nlohmann::json data = nlohmann::json::parse(text, nullptr, false);
                if (data.is_discarded() || !data.is_object() || !data.contains("name")) return SHARD_E_INVALID_ARG;
                auto spawned = Worlds::InstantiateActor(*at->world, data, parentActor);
                if (!spawned) return SHARD_E_INVALID_ARG;
                *out = ActorHandle(spawned);
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
                    // The first root actor, in the order of the ids
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
        static const ShardWorldsApi api = { sizeof(ShardWorldsApi), &LoadWorld, &IsLoading, &LoadedCount, &SpawnPrefab, &WorldAt };
        return &api;
    }
}

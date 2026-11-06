// Shard C++ SDK : loading worlds (shard_worlds.h). Prefabs are spawned with Scene::SpawnPrefab.
//
//   if (reachedExit) Shard::Worlds::Load("worlds/level2.world");
//
// The load is a request : the engine swaps the worlds at the start of a later frame, so the script that asks keeps running until then.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <shard/shard_worlds.h>
#include <shard/cpp/actor.hpp>

namespace Shard {

    class Worlds {
    public:
        /// Asks for the world at that path of the project to replace the loaded ones. False : the project has no such world, or a load is already
        /// under way.
        static bool Load(std::string_view pathInProject) { return detail::WorldsApi().load_world(Str(pathInProject)) == SHARD_OK; }
        /// True while a requested load is under way
        static bool IsLoading() { return detail::WorldsApi().is_loading() == SHARD_TRUE; }
        static int LoadedCount() { return detail::WorldsApi().loaded_count(); }

        /// A loaded world (ABI 1.7) : its asset id (-1 if it has none), its name and an actor to use as the anchor of Scene (its first root actor,
        /// invalid for an empty world)
        struct Loaded {
            int32_t assetId = -1;
            std::string name;
            Actor anchor;
        };
        /// The loaded world at an index (0 .. LoadedCount() - 1), if there is one
        static std::optional<Loaded> At(int index)
        {
            const ShardWorldsApi* api = detail::Interface<ShardWorldsApi>(SHARD_IFACE_WORLDS, 1);
            if (!api || index < 0) return std::nullopt;
            Loaded out;
            ShardActor anchor = 0;
            std::string name;
            const ShardResult r = ReadText([&](char* b, uint64_t c, uint64_t* s) { return api->world_at(index, &out.assetId, b, c, s, &anchor); }, name);
            if (r != SHARD_OK) return std::nullopt;
            out.name = std::move(name);
            out.anchor = Actor(anchor);
            return out;
        }
    };
}

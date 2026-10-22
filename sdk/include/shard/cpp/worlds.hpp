// Shard C++ SDK : loading worlds (shard_worlds.h). Prefabs are spawned with Scene::SpawnPrefab.
//
//   if (reachedExit) Shard::Worlds::Load("worlds/level2.world");
//
// The load is a request : the engine swaps the worlds at the start of a later frame, so the script that asks keeps running until then.
#pragma once

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
    };
}

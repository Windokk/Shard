/* Interface "shard.worlds" 1.0 : loading worlds, and prefabs.
 *
 * A world is loaded by its path in the project ("worlds/arena.world"). The load is a request : it starts in the background, and the world
 * is swapped in at the start of a following frame, where the old worlds are unloaded (their scripts get on_destroyed) and the new one
 * is loaded (the plugin's on_world_loaded is called). A script that asks for a load keeps running until then, so nothing it holds is
 * pulled from under it in the middle of a frame.
 *
 * A prefab is an actor stored as JSON in the content of a module (prefabs/<name>.json, shard_content.h) : the form the editor writes an
 * actor in. Spawning one builds the actor, its components and its children.
 *
 * thread : script (shard_actors.h says where that is). */
#ifndef SHARD_WORLDS_H
#define SHARD_WORLDS_H

#include "shard_abi.h"
#include "shard_actors.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_WORLDS "shard.worlds"

typedef struct ShardWorldsApi {
    uint32_t structSize;

    /* Asks for that world to replace the loaded ones. SHARD_E_NOT_FOUND : the project has no such world. SHARD_E_ALREADY_EXISTS : a load is
     * already under way (ask again once is_loading says it is over). SHARD_E_UNSUPPORTED : this host has no worlds. */
    ShardResult (SHARD_CALL *load_world)(ShardStr path_in_project);
    /* SHARD_TRUE while a requested load is under way. */
    int32_t     (SHARD_CALL *is_loading)(void);
    /* How many worlds are loaded. */
    int32_t     (SHARD_CALL *loaded_count)(void);

    /* Builds the prefab in the world of `anchor`, under `parent` (0 : at the root), and returns its actor. `prefab` is a mod:// address
     * ("mod://com.acme.game/prefabs/crate.json"), a logical path ("prefabs/crate.json"), or a bare name ("crate", read as
     * "prefabs/crate.json"). SHARD_E_NOT_FOUND : no such file. SHARD_E_INVALID_ARG : it is not an actor. */
    ShardResult (SHARD_CALL *spawn_prefab)(ShardActor anchor, ShardStr prefab, ShardActor parent, ShardActor* out);
} ShardWorldsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_WORLDS_H */

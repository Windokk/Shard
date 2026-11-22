/* Interface "shard.worlds" 1.2 : loading worlds, and prefabs ; which worlds are loaded (1.1) ; prefab instances, found and destroyed as a whole (1.2).
 *
 * A world is loaded by its path in the project ("worlds/arena.world"). The load is a request : it starts in the background, and the world
 * is swapped in at the start of a following frame, where the old worlds are unloaded (their scripts get on_destroyed) and the new one
 * is loaded (the plugin's on_world_loaded is called). A script that asks for a load keeps running until then, so nothing it holds is
 * pulled from under it in the middle of a frame.
 *
 * A prefab is an asset of the project (prefabs/<name>.prefab) : an actor stored as JSON, with its components and its children. It is also
 * found in the content of a module (prefabs/<name>.json, shard_content.h). Spawning one builds the actor, its components and its
 * children : an instance, whose root remembers the prefab it comes from. An instance is one unit : it is found by the name of its prefab,
 * and destroyed whole from any of its actors. A world file keeps an instance as a link to its prefab and the differences from it.
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

/* spawn_prefab_ex flags */
#define SHARD_PREFAB_SET_TRANSFORM  0x1u    /* `transform` is the pose of the instance (relative to `parent`), applied before its children are built */

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

    /* ---- 1.1 ---- */
    /* The loaded world at an index (0 .. loaded_count - 1, the order they were loaded in) : its asset id (-1 if it has none), its name as a
     * "text out", and an actor to use as the `anchor` of the functions that work "in the world of" one -- its first root actor, or 0 if the
     * world is empty. Any of the pointers may be NULL. SHARD_E_NOT_FOUND : past the end. */
    ShardResult (SHARD_CALL *world_at)(int32_t index, int32_t* asset_id, char* name, uint64_t capacity, uint64_t* size, ShardActor* anchor);

    /* ---- 1.2 ---- */
    /* spawn_prefab with options. `parent` : 0 for the root of the world. `flags` : SHARD_PREFAB_*, `transform` is read only with
     * SHARD_PREFAB_SET_TRANSFORM (it may be NULL otherwise). `name` : the name of the instance's root, empty for the prefab's.
     * `overrides_json` : the differences from the prefab, as a world file writes them in "overrides" ("{\"components\":{\"light\":{\"intensity\":3}}}"),
     * empty for none. SHARD_E_INVALID_ARG : bad overrides, or the prefab is not an actor. */
    ShardResult (SHARD_CALL *spawn_prefab_ex)(ShardActor anchor, ShardStr prefab, ShardActor parent, uint32_t flags, const ShardLocalTransform* transform,
                                              ShardStr name, ShardStr overrides_json, ShardActor* out);
    /* The roots of the instances of a prefab, in the order of their ids, in the world of `anchor` : up to `capacity` into `out`, how many
     * there are in *count (call it with capacity 0 to ask). `prefab` is its name ("crate"), or its path ("prefabs/crate.prefab") to tell
     * two prefabs of the same name apart. The deactivated ones are left out. */
    ShardResult (SHARD_CALL *find_prefab)(ShardActor anchor, ShardStr prefab, ShardActor* out, uint32_t capacity, uint32_t* count);
    /* The instance that the actor belongs to : the path of its prefab as a "text out" (any pointer may be NULL), and its root.
     * SHARD_E_NOT_FOUND : the actor is not part of an instance. */
    ShardResult (SHARD_CALL *get_prefab)(ShardActor actor, char* name, uint64_t capacity, uint64_t* size, ShardActor* root);
    /* Destroys the whole instance that the actor belongs to (its root, its children). SHARD_E_NOT_FOUND : not part of an instance. */
    ShardResult (SHARD_CALL *destroy_prefab)(ShardActor actor);
    /* Destroys every instance of a prefab in the world of `anchor`. *count (may be NULL) : how many. */
    ShardResult (SHARD_CALL *destroy_prefabs)(ShardActor anchor, ShardStr prefab, uint32_t* count);
} ShardWorldsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_WORLDS_H */

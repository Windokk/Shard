/* Interface "shard.ecs" 1.1 : entities and their components, from inside a system (shard_systems.h).
 *
 * Every function takes the ShardEcs the system was handed in its ShardSystemCtx : it is valid during that run, on that thread, and
 * nowhere else (SHARD_E_STALE_HANDLE). A system reads and writes only the types it declared (reads / writes of its ShardSystemDesc,
 * SHARD_E_INVALID_ARG otherwise ; an exclusive system may touch anything) : the scheduler relies on these declarations to run systems
 * in parallel, and the result of a frame must not depend on it.
 *
 * Structural changes (spawn, destroy, add, remove, set_parent) are recorded and applied at the end of the phase, system by system in
 * registration order. spawn returns a placeholder that the later commands of the same run can use ; it means nothing afterwards.
 * Changing the VALUE of a component (through the pointers of each_chunk and get) is immediate.
 *
 * Moving an actor (1.1) : the engine's own components below are not written through these pointers, the actor's Transform is what drives
 * them. set_local_transform records the new pose ; at the end of the phase the actor's Transform takes it (so does everything that follows
 * a Transform : children, physics, the renderer) and the LocalTransform of the entity then holds it. */
#ifndef SHARD_ECS_H
#define SHARD_ECS_H

#include "shard_abi.h"
#include "shard_components.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_ECS "shard.ecs"

typedef uint64_t ShardEntity;   /* generation << 32 | index ; 0 is the null entity */
typedef uint64_t ShardEcs;      /* the registry as one system run sees it */

typedef struct ShardQuery {
    uint32_t structSize;
    uint32_t withCount;
    uint32_t withoutCount;
    const ShardComponentType* with;     /* the entities have all of these ; the chunk's columns come in this order */
    const ShardComponentType* without;  /* and none of these */
} ShardQuery;

/* `count` entities that have the same components. columns[i] holds `count` blocks of query.with[i], one after the other : block j is at
 * (char*)columns[i] + j * size of that type. Valid during the callback only. */
typedef struct ShardChunk {
    uint32_t structSize;
    uint32_t count;
    const ShardEntity* entities;
    void* const* columns;
} ShardChunk;

/* Return SHARD_OK to go on ; anything else stops the iteration and is what each_chunk returns. */
typedef ShardResult (SHARD_CALL *ShardChunkFn)(void* user, const ShardChunk* chunk);

/* "shard.LocalTransform" (find_type) : on the entity of every actor, its Transform relative to its parent. Read it ; move the actor with
 * set_local_transform (1.1) */
typedef struct ShardLocalTransform {
    float position[3];
    float rotation[4];      /* quaternion x, y, z, w */
    float scale[3];
} ShardLocalTransform;

typedef struct ShardEcsApi {
    uint32_t structSize;

    /* Calls fn once per chunk of matching entities, in a stable order. No structural change happens meanwhile. thread : system */
    ShardResult (SHARD_CALL *each_chunk)(ShardEcs ecs, const ShardQuery* query, ShardChunkFn fn, void* user);
    /* How many entities match. thread : system */
    ShardResult (SHARD_CALL *count)(ShardEcs ecs, const ShardQuery* query, uint64_t* out);

    /* SHARD_TRUE if the entity exists (a placeholder of spawn does not, yet). thread : system */
    int32_t     (SHARD_CALL *is_alive)(ShardEcs ecs, ShardEntity entity);
    /* The entity's block of `type`, valid until the end of the run. SHARD_E_NOT_FOUND : it has none (or is dead). thread : system */
    ShardResult (SHARD_CALL *get)(ShardEcs ecs, ShardEntity entity, ShardComponentType type, void** out);
    /* The parent (the null entity for a root). thread : system */
    ShardResult (SHARD_CALL *parent)(ShardEcs ecs, ShardEntity entity, ShardEntity* out);

    /* Recorded, applied at the end of the phase. thread : system */
    ShardResult (SHARD_CALL *spawn)(ShardEcs ecs, ShardEntity* placeholder);
    ShardResult (SHARD_CALL *destroy)(ShardEcs ecs, ShardEntity entity);                  /* and its descendants */
    /* Adds the component, or overwrites its value. `size` must be the size of the type ; `data` is copied now. */
    ShardResult (SHARD_CALL *add)(ShardEcs ecs, ShardEntity entity, ShardComponentType type, const void* data, uint64_t size);
    ShardResult (SHARD_CALL *remove)(ShardEcs ecs, ShardEntity entity, ShardComponentType type);
    ShardResult (SHARD_CALL *set_parent)(ShardEcs ecs, ShardEntity child, ShardEntity parent);  /* null parent : detach */

    /* ---- 1.1 : moving actors ---- */
    /* Records the new local pose (relative to the parent) of the actor that stands behind `entity`. Applied at the end of the phase, on the
     * main thread, after the structural changes ; when several systems set the same entity, the one whose name sorts last wins. The system
     * must have "shard.LocalTransform" in its `writes` (shard.systems). `transform` is read now.
     * SHARD_E_NOT_FOUND : the entity has no actor now (a placeholder, or an entity a plugin spawned itself) : nothing moves. An actor that is
     * deactivated when the phase ends ignores it. thread : system */
    ShardResult (SHARD_CALL *set_local_transform)(ShardEcs ecs, ShardEntity entity, const ShardLocalTransform* transform);
} ShardEcsApi;

/* ---- The engine's own components, found by name with shard.components' find_type ----
 * Read them ; do not write their blocks : the actor's components are what drives them, and they overwrite what a system writes. */

/* "shard.ActorLink" : on the entity of every actor, the actor's object id */
typedef struct ShardActorLink {
    int32_t objectId;
} ShardActorLink;

/* "shard.Disabled" (1 byte, no field) : on the entity of an actor that is deactivated. Put it in `without` to skip those. */

#ifdef __cplusplus
}
#endif

#endif /* SHARD_ECS_H */

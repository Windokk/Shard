// Valid only during that run, on that thread. A system touches only the types it declared, the scheduler runs systems in parallel on that basis.
// Structural changes are recorded and applied at phase end in registration order. Value changes are immediate.
#ifndef SHARD_ECS_H
#define SHARD_ECS_H

#include "shard_abi.h"
#include "shard_components.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_ECS "shard.ecs"

typedef uint64_t ShardEntity;
typedef uint64_t ShardEcs;

typedef struct ShardQuery {
    uint32_t structSize;
    uint32_t withCount;
    uint32_t withoutCount;
    const ShardComponentType* with;
    const ShardComponentType* without;
} ShardQuery;

// `count` entities with the same components. Block j of columns[i] is at (char*)columns[i] + j * size of that type. Valid during the callback only.
typedef struct ShardChunk {
    uint32_t structSize;
    uint32_t count;
    const ShardEntity* entities;
    void* const* columns;
} ShardChunk;

typedef ShardResult (SHARD_CALL *ShardChunkFn)(void* user, const ShardChunk* chunk);

typedef struct ShardEcsApi {
    uint32_t structSize;

    ShardResult (SHARD_CALL *each_chunk)(ShardEcs ecs, const ShardQuery* query, ShardChunkFn fn, void* user);
    ShardResult (SHARD_CALL *count)(ShardEcs ecs, const ShardQuery* query, uint64_t* out);

    int32_t     (SHARD_CALL *is_alive)(ShardEcs ecs, ShardEntity entity);
    ShardResult (SHARD_CALL *get)(ShardEcs ecs, ShardEntity entity, ShardComponentType type, void** out);
    ShardResult (SHARD_CALL *parent)(ShardEcs ecs, ShardEntity entity, ShardEntity* out);

    ShardResult (SHARD_CALL *spawn)(ShardEcs ecs, ShardEntity* placeholder);
    ShardResult (SHARD_CALL *destroy)(ShardEcs ecs, ShardEntity entity);                  // and its descendants
    ShardResult (SHARD_CALL *add)(ShardEcs ecs, ShardEntity entity, ShardComponentType type, const void* data, uint64_t size);
    ShardResult (SHARD_CALL *remove)(ShardEcs ecs, ShardEntity entity, ShardComponentType type);
    ShardResult (SHARD_CALL *set_parent)(ShardEcs ecs, ShardEntity child, ShardEntity parent);
} ShardEcsApi;

// Engine components: read them, do not write. The actor's components drive them and overwrite system writes.

typedef struct ShardLocalTransform {
    float position[3];
    float rotation[4];
    float scale[3];
} ShardLocalTransform;

typedef struct ShardActorLink {
    int32_t objectId;
} ShardActorLink;

#ifdef __cplusplus
}
#endif

#endif

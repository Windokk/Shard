#ifndef SHARD_SYSTEMS_H
#define SHARD_SYSTEMS_H

#include "shard_abi.h"
#include "shard_components.h"
#include "shard_ecs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_SYSTEMS "shard.systems"

#define SHARD_PHASE_INPUT           0u
#define SHARD_PHASE_PRE_SIM         1u
#define SHARD_PHASE_FIXED           2u
#define SHARD_PHASE_POST_PHYSICS    3u
#define SHARD_PHASE_UPDATE          4u
#define SHARD_PHASE_LATE            5u
#define SHARD_PHASE_EXTRACT         6u

#define SHARD_SYSTEM_EXCLUSIVE      1u  // touches what reads/writes cannot say: runs alone, in order
#define SHARD_SYSTEM_MAIN_THREAD    2u  // needs the frame's thread: the phase then runs on it, in order

typedef struct ShardSystemCtx {
    uint32_t structSize;
    float    deltaTime;
    float    fixedDeltaTime;
    int32_t  fixedStep;
    int32_t  fixedStepCount;
    uint64_t frame;
    ShardEcs ecs;
} ShardSystemCtx;

// Any result other than SHARD_OK counts as a plugin error. At the tenth the host fails the plugin.
typedef ShardResult (SHARD_CALL *ShardSystemFn)(void* user, ShardPluginCtx ctx, const ShardSystemCtx* run);

typedef struct ShardSystemDesc {
    uint32_t structSize;
    uint32_t phase;
    uint32_t flags;
    uint32_t readCount, writeCount;
    uint32_t afterCount, beforeCount;
    ShardStr name;
    const ShardComponentType* reads;
    const ShardComponentType* writes;
    const ShardStr* after;
    const ShardStr* before;
    ShardSystemFn run;
    void* user;
} ShardSystemDesc;

typedef struct ShardSystemsApi {
    uint32_t structSize;
    ShardResult (SHARD_CALL *add)(ShardPluginCtx ctx, const ShardSystemDesc* desc);
    ShardResult (SHARD_CALL *set_enabled)(ShardPluginCtx ctx, ShardStr name, int32_t enabled);
    ShardResult (SHARD_CALL *remove)(ShardPluginCtx ctx, ShardStr name);
} ShardSystemsApi;

#ifdef __cplusplus
}
#endif

#endif

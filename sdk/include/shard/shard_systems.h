/* Interface "shard.systems" 1.0 : functions the engine's scheduler runs every frame, in a phase, in parallel with the systems they do not
 * conflict with (one writes what the other reads or writes). A system is named "<plugin id>/<name>" by the host and is removed with the
 * plugin. A system added before the engine built its scheduler (from on_load) starts with the first frame. */
#ifndef SHARD_SYSTEMS_H
#define SHARD_SYSTEMS_H

#include "shard_abi.h"
#include "shard_components.h"
#include "shard_ecs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_SYSTEMS "shard.systems"

/* The phases of a frame, always run in this order. Fixed runs 0..N times per frame (as many fixed steps as the elapsed time pays for). */
#define SHARD_PHASE_INPUT           0u
#define SHARD_PHASE_PRE_SIM         1u
#define SHARD_PHASE_FIXED           2u
#define SHARD_PHASE_POST_PHYSICS    3u
#define SHARD_PHASE_UPDATE          4u
#define SHARD_PHASE_LATE            5u
#define SHARD_PHASE_EXTRACT         6u

#define SHARD_SYSTEM_EXCLUSIVE      1u  /* touches what reads / writes cannot say : runs alone, in order */
#define SHARD_SYSTEM_MAIN_THREAD    2u  /* needs the thread that runs the frame : the phase then runs on it, in order */

/* What a run of a system is given. Valid during the run only. */
typedef struct ShardSystemCtx {
    uint32_t structSize;
    float    deltaTime;         /* the variable step ; the fixed step in SHARD_PHASE_FIXED */
    float    fixedDeltaTime;
    int32_t  fixedStep;         /* SHARD_PHASE_FIXED : index of this step in the frame, from 0 */
    int32_t  fixedStepCount;    /* SHARD_PHASE_FIXED : how many steps this frame runs */
    uint64_t frame;
    ShardEcs ecs;               /* for shard.ecs */
} ShardSystemCtx;

/* Returns SHARD_OK. Any other result is counted as an error of the plugin ; at the tenth, the host fails it. thread : the system's */
typedef ShardResult (SHARD_CALL *ShardSystemFn)(void* user, ShardPluginCtx ctx, const ShardSystemCtx* run);

typedef struct ShardSystemDesc {
    uint32_t structSize;
    uint32_t phase;                         /* SHARD_PHASE_* */
    uint32_t flags;                         /* SHARD_SYSTEM_* */
    uint32_t readCount, writeCount;
    uint32_t afterCount, beforeCount;
    ShardStr name;                          /* "rotate" : registered as "<plugin id>/rotate" */
    const ShardComponentType* reads;
    const ShardComponentType* writes;
    /* Explicit order, systems of the same phase only. A full name ("com.acme.weather/wind"), or "./x" for this plugin's "x". */
    const ShardStr* after;
    const ShardStr* before;
    ShardSystemFn run;
    void* user;                             /* handed back to run */
} ShardSystemDesc;

typedef struct ShardSystemsApi {
    uint32_t structSize;
    /* Everything in desc is copied. SHARD_E_ALREADY_EXISTS : this plugin has a system of that name. thread : main */
    ShardResult (SHARD_CALL *add)(ShardPluginCtx ctx, const ShardSystemDesc* desc);
    /* `name` as given to add. thread : main. SHARD_E_NOT_FOUND : no such system of this plugin. */
    ShardResult (SHARD_CALL *set_enabled)(ShardPluginCtx ctx, ShardStr name, int32_t enabled);
    ShardResult (SHARD_CALL *remove)(ShardPluginCtx ctx, ShardStr name);
} ShardSystemsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_SYSTEMS_H */

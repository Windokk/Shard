/* Interface "shard.jobs" 1.0 : the engine's worker threads, for a plugin's own work.
 *
 * A plugin that bakes a navigation mesh, meshes a chunk of voxels, finds a path, packs a network message or evaluates a pose should not start
 * threads of its own next to the engine's : the engine already has one pool, sized for the machine and shared by the renderer, the physics
 * and the scheduler. These functions run a plugin's work on it.
 *
 * What a job may call : the functions the headers document as "thread : any" (blobs, shard.time, shard.assets reads, shard.render's mesh and
 * texture creation, shard.debug, shard.events emit...) and the plugin's own code. NOT the ones that say "thread : main", "system" or "script"
 * (actors, ECS, physics, input, editor...) : a job runs beside the frame, and none of those is made for that (SHARD_E_WRONG_THREAD). To get
 * a result back to the main thread, call post_main from the job.
 *
 * A job that is still running when the plugin is disabled is waited for first : the host never calls a function of a library it is about to
 * unload. A job that never ends therefore blocks the disabling : keep them short, or check a flag of your own.
 *
 * thread : any, for every function except `wait` and `parallel_for` (below). A sandboxed plugin (tier 2) is not offered this interface. */
#ifndef SHARD_JOBS_H
#define SHARD_JOBS_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_JOBS "shard.jobs"

typedef uint64_t ShardJob;      /* a job (or a group of them) in flight ; 0 is never valid */

/* `worker` : 0 for the thread that waits (it helps while it waits), 1.. for the pool's threads. Stable for the call : index a per-worker scratch
 * buffer with it. */
typedef void (SHARD_CALL *ShardJobFn)(void* user, uint32_t worker);
/* One slice [begin, end) of a parallel_for */
typedef void (SHARD_CALL *ShardRangeFn)(void* user, uint32_t begin, uint32_t end, uint32_t worker);
/* thread : main */
typedef void (SHARD_CALL *ShardMainFn)(void* user);

typedef struct ShardJobsApi {
    uint32_t structSize;

    /* How many threads work for the engine, the calling one included : the size to give a per-worker array. */
    uint32_t    (SHARD_CALL *worker_count)(void);

    /* Runs `fn` on a worker, as soon as one is free. `out` receives a handle to wait for, or may be NULL : the job is then fire and forget
     * (the host still waits for it when the plugin is disabled). The jobs of a plugin start in the order they were submitted ; they do not
     * end in it. */
    ShardResult (SHARD_CALL *run)(ShardPluginCtx ctx, ShardJobFn fn, void* user, ShardJob* out);

    /* Calls `fn` on slices of [0, count) of about `grain` indices each (0 : the host picks), on every worker at once, and returns when all
     * of them are done. The slices are disjoint ; their order is not defined, so the result must not depend on it (a replay does not
     * reproduce the order : write to a slot per index, then combine in index order). thread : main, script or system. */
    ShardResult (SHARD_CALL *parallel_for)(ShardPluginCtx ctx, uint32_t count, uint32_t grain, ShardRangeFn fn, void* user);
    /* The same, without waiting : `out` is a handle to wait for. */
    ShardResult (SHARD_CALL *parallel_for_async)(ShardPluginCtx ctx, uint32_t count, uint32_t grain, ShardRangeFn fn, void* user, ShardJob* out);

    /* SHARD_TRUE once the job (or all the slices) ended. Does not release the handle. */
    int32_t     (SHARD_CALL *is_done)(ShardPluginCtx ctx, ShardJob job);
    /* Returns when the job ended, running other jobs meanwhile, and releases the handle. SHARD_E_STALE_HANDLE : already waited for, or not
     * this plugin's. thread : main, script or system. */
    ShardResult (SHARD_CALL *wait)(ShardPluginCtx ctx, ShardJob job);
    /* Releases the handle without waiting (the job still runs to its end). */
    ShardResult (SHARD_CALL *release)(ShardPluginCtx ctx, ShardJob job);

    /* Calls `fn` on the main thread, at the start of the next frame, in the order the posts were made by one thread (posts of several threads
     * are ordered by the host, not by you). How a job, a network thread of your own or a callback of a library hands its result to the
     * simulation. Posts that are still waiting when the plugin is disabled are dropped. */
    ShardResult (SHARD_CALL *post_main)(ShardPluginCtx ctx, ShardMainFn fn, void* user);
} ShardJobsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_JOBS_H */

/* Interface "shard.hooks" 1.0 : the points of the engine a plugin can intercept, and cancel. Hooks are found by name ; every one is described
 * by the same struct, whose `subject`, `detail` and `frame` mean what the table below says. Handlers run by priority, then by plugin load
 * order, then in the order they were added : the same order on every run. They are removed with the plugin.
 *
 *   name                 subject             detail          frame
 *   "PreEngineInit"      -                   -               -
 *   "PostEngineInit"     -                   -               -
 *   "PreWorldLoad"       the world's name    -               -       cancellable : the world is not loaded
 *   "PostWorldLoad"      the world's name    -               -
 *   "PrePlay"            -                   -               -       cancellable : play mode does not start
 *   "PostPlay"           -                   -               -
 *   "PreFrame"           -                   -               frame
 *   "PostFrame"          -                   -               frame
 *   "PreSave"            what is saved       -               -       cancellable : nothing is saved
 *   "AssetImported"      the asset's path    -               -
 *   "PlayerConnecting"   the client's name   its address     -       cancellable : the client is refused, `reason` is what it is told
 *   "PreShutdown"        -                   -               - */
#ifndef SHARD_HOOKS_H
#define SHARD_HOOKS_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_HOOKS "shard.hooks"

#define SHARD_PRIORITY_EARLIEST (-200)
#define SHARD_PRIORITY_EARLY    (-100)
#define SHARD_PRIORITY_NORMAL   0
#define SHARD_PRIORITY_LATE     100
#define SHARD_PRIORITY_LATEST   200

typedef uint64_t ShardHookToken;    /* 0 is never valid */

typedef struct ShardHookArgs {
    uint32_t structSize;
    uint8_t  cancelled;             /* in : an earlier handler cancelled ; out : set it to 1 to cancel (a later handler may see it and set it back) */
    uint8_t  reserved[3];
    ShardStr name;                  /* the hook's name */
    ShardStr subject;               /* see the table ; valid for the call */
    ShardStr detail;
    uint64_t frame;
    ShardStr reason;                /* out : why it was cancelled, copied when the handler returns */
} ShardHookArgs;

/* thread : main */
typedef void (SHARD_CALL *ShardHookFn)(void* user, ShardPluginCtx ctx, ShardHookArgs* hook);

typedef struct ShardHooksApi {
    uint32_t structSize;
    /* `name` from the table above (SHARD_E_NOT_FOUND otherwise). thread : main */
    ShardResult (SHARD_CALL *on)(ShardPluginCtx ctx, ShardStr name, int32_t priority, ShardHookFn fn, void* user, ShardHookToken* out);
    ShardResult (SHARD_CALL *off)(ShardPluginCtx ctx, ShardHookToken token);
} ShardHooksApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_HOOKS_H */

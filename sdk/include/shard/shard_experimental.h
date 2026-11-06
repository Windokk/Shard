/* Interface "shard.experimental.world_ecs" 0.1 : the engine's entities from the main thread, outside the systems.
 *
 * The interfaces named "shard.experimental.*" are offered so that what may still change can be tried before it is frozen into the ABI. They
 * carry NO compatibility promise : one may change or disappear in any revision of the engine. Their major is 0, and query_interface hands
 * one out only for the exact minor the engine has (query_interface("shard.experimental.world_ecs", 0, 1, ...)) : a plugin built against
 * another one does not get it. A native plugin that carries a valid signature of a trusted publisher (what is distributed) is refused them :
 * every function returns SHARD_E_UNSUPPORTED. Their structs are not pinned by the layout tests.
 *
 * world_ecs : open() gives a ShardEcs over the engine's registry that every function of shard.ecs (shard_ecs.h) accepts, as an exclusive
 * system would (any type) ; the structural changes are applied by close(). Between the two, the main thread functions of the other
 * interfaces keep working. Open and close in the same callback ; what a plugin leaves open is closed when its context goes. */
#ifndef SHARD_EXPERIMENTAL_H
#define SHARD_EXPERIMENTAL_H

#include "shard_abi.h"
#include "shard_ecs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_EXPERIMENTAL_WORLD_ECS "shard.experimental.world_ecs"

typedef struct ShardWorldEcsApi {
    uint32_t structSize;
    /* thread : main, outside the systems and while no phase runs (SHARD_E_WRONG_THREAD). SHARD_E_UNSUPPORTED : the plugin may not use
     * experimental interfaces, or this host has no registry (an offline tool). */
    ShardResult (SHARD_CALL *open)(ShardPluginCtx ctx, ShardEcs* out);
    /* Applies the structural changes recorded since open, in order. The last opened first. */
    ShardResult (SHARD_CALL *close)(ShardPluginCtx ctx, ShardEcs ecs);
} ShardWorldEcsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_EXPERIMENTAL_H */

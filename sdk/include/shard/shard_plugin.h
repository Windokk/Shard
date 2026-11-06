/* Interface "shard.plugin" 1.2 : who the plugin is, as the host read it from the manifest, and where it runs. Thread : any.
 * 1.1 added host and has_permission : ask for minor 1 (query_interface(SHARD_IFACE_PLUGIN, 1, 1, ...)) to call them.
 * 1.2 added random_stream : ask for minor 2. */
#ifndef SHARD_PLUGIN_H
#define SHARD_PLUGIN_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_PLUGIN "shard.plugin"

/* The state of a PCG32 generator (O'Neill 2014) : `state` and `increment` are the two words of Shard's Random class. Whoever holds them
 * continues the sequence with the same code and gets the same numbers on every platform (shard/cpp/random.hpp does). */
typedef struct ShardRandomState {
    uint64_t state;
    uint64_t increment;
} ShardRandomState;

typedef struct ShardPluginApi {
    uint32_t structSize;
    /* The returned strings stay valid until the plugin is unloaded. */
    ShardStr (SHARD_CALL *id)(ShardPluginCtx ctx);              /* "com.acme.weather" */
    ShardStr (SHARD_CALL *version)(ShardPluginCtx ctx);         /* "1.4.2" */
    ShardStr (SHARD_CALL *directory)(ShardPluginCtx ctx);       /* the plugin's folder, absolute, '/' separators */

    /* ---- 1.1 ---- */
    /* The executable the plugin runs in : "editor", "player", "headless" or "tool" */
    ShardStr (SHARD_CALL *host)(ShardPluginCtx ctx);
    /* SHARD_TRUE if the manifest asks for that permission ("fs.read:content", "net"...) */
    int32_t  (SHARD_CALL *has_permission)(ShardPluginCtx ctx, ShardStr permission);

    /* ---- 1.2 ---- */
    /* A generator for the stream `name` : derived from the run's seed, the plugin's id and the name, so the same name gives the same
     * sequence in a replay of the run, and two plugins never share a stream. Name a stream after what draws from it (an actor's name and
     * the script's), not after the order things started in. The generator is the caller's afterwards : no further call is needed. */
    ShardResult (SHARD_CALL *random_stream)(ShardPluginCtx ctx, ShardStr name, ShardRandomState* out);
} ShardPluginApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_PLUGIN_H */

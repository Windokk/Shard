/* Interface "shard.plugin" 1.1 : who the plugin is, as the host read it from the manifest, and where it runs. Thread : any.
 * 1.1 added host and has_permission : ask for minor 1 (query_interface(SHARD_IFACE_PLUGIN, 1, 1, ...)) to call them. */
#ifndef SHARD_PLUGIN_H
#define SHARD_PLUGIN_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_PLUGIN "shard.plugin"

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
} ShardPluginApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_PLUGIN_H */

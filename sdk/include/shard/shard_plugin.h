/* Interface "shard.plugin" 1.0 : who the plugin is, as the host read it from the manifest. Thread : any. */
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
} ShardPluginApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_PLUGIN_H */

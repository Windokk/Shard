#ifndef SHARD_PLUGIN_H
#define SHARD_PLUGIN_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_PLUGIN "shard.plugin"

typedef struct ShardPluginApi {
    uint32_t structSize;
    ShardStr (SHARD_CALL *id)(ShardPluginCtx ctx);
    ShardStr (SHARD_CALL *version)(ShardPluginCtx ctx);
    ShardStr (SHARD_CALL *directory)(ShardPluginCtx ctx);
} ShardPluginApi;

#ifdef __cplusplus
}
#endif

#endif

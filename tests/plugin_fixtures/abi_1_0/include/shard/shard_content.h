#ifndef SHARD_CONTENT_H
#define SHARD_CONTENT_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_CONTENT "shard.content"

typedef ShardResult (SHARD_CALL *ShardListFn)(void* user, ShardStr path);

typedef struct ShardContentApi {
    uint32_t structSize;
    ShardResult (SHARD_CALL *uri_of)(ShardPluginCtx ctx, ShardStr path, char* buffer, uint64_t capacity, uint64_t* size);
    ShardResult (SHARD_CALL *resolve)(ShardPluginCtx ctx, ShardStr uri, char* buffer, uint64_t capacity, uint64_t* size);
    ShardResult (SHARD_CALL *resolve_logical)(ShardPluginCtx ctx, ShardStr logicalPath, char* buffer, uint64_t capacity, uint64_t* size);
    ShardResult (SHARD_CALL *read)(ShardPluginCtx ctx, ShardStr uri, void** data, uint64_t* size);
    ShardResult (SHARD_CALL *list)(ShardPluginCtx ctx, ShardStr pluginId, ShardStr subdir, ShardListFn fn, void* user);
} ShardContentApi;

#ifdef __cplusplus
}
#endif

#endif

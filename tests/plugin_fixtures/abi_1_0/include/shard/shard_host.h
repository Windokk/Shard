#ifndef SHARD_HOST_H
#define SHARD_HOST_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

// One static table for every plugin, which designates itself with the ShardPluginCtx it is handed. Only the base is here, the rest comes through query_interface.

typedef struct ShardHostApi {
    uint32_t structSize;
    uint32_t abiMajor, abiMinor;
    ShardStr engineVersion;

    void        (SHARD_CALL *log)(ShardPluginCtx ctx, uint32_t level, ShardStr message);
    // Counted against the plugin's budget (manifest budgets.memoryMB). Free with the host's `free` only, never the plugin's or the host's own allocator.
    void*       (SHARD_CALL *alloc)(ShardPluginCtx ctx, uint64_t size, uint64_t align);
    void        (SHARD_CALL *free)(ShardPluginCtx ctx, void* p);
    // `major` must match, interface at least `minMinor`. *out is a struct starting with structSize, valid until the host exits.
    ShardResult (SHARD_CALL *query_interface)(ShardStr id, uint32_t major, uint32_t minMinor, const void** out);
} ShardHostApi;

typedef struct ShardWorldInfo {
    uint32_t structSize;
    int32_t  assetId;
    ShardStr name;
} ShardWorldInfo;

// Same order and meaning as IPlugin. All optional (NULL = nothing to do), all on the main thread. An error fails the plugin and its dependents.

typedef struct ShardPluginCallbacks {
    uint32_t structSize;
    ShardResult (SHARD_CALL *on_load)(void* user, ShardPluginCtx ctx);
    ShardResult (SHARD_CALL *on_initialize)(void* user, ShardPluginCtx ctx);
    ShardResult (SHARD_CALL *on_enable)(void* user, ShardPluginCtx ctx);
    ShardResult (SHARD_CALL *on_disable)(void* user, ShardPluginCtx ctx);
    ShardResult (SHARD_CALL *on_unload)(void* user, ShardPluginCtx ctx);

    // on_prepare_reload writes what must survive into `state`. Any other result than SHARD_OK (or no callback) makes the plugin not reloadable. on_restore runs in the new binary after on_enable.
    ShardResult (SHARD_CALL *on_prepare_reload)(void* user, ShardPluginCtx ctx, ShardState state);
    ShardResult (SHARD_CALL *on_restore)(void* user, ShardPluginCtx ctx, ShardState state);

    // Keep short: their time is billed to the plugin.
    ShardResult (SHARD_CALL *on_frame_begin)(void* user, ShardPluginCtx ctx, uint64_t frame);
    ShardResult (SHARD_CALL *on_frame_end)(void* user, ShardPluginCtx ctx, uint64_t frame);

    ShardResult (SHARD_CALL *on_content_mounted)(void* user, ShardPluginCtx ctx);
    ShardResult (SHARD_CALL *on_world_loaded)(void* user, ShardPluginCtx ctx, const ShardWorldInfo* world);
    ShardResult (SHARD_CALL *on_world_unloaded)(void* user, ShardPluginCtx ctx, const ShardWorldInfo* world);
    ShardResult (SHARD_CALL *on_play_start)(void* user, ShardPluginCtx ctx, const ShardWorldInfo* world);
    ShardResult (SHARD_CALL *on_play_stop)(void* user, ShardPluginCtx ctx, const ShardWorldInfo* world);

    // An error here cancels the task but does not fail the plugin, unlike the other callbacks. *message is copied before return.
    ShardResult (SHARD_CALL *on_tool_task)(void* user, ShardPluginCtx ctx, ShardStr task, ShardStr project, ShardStr* message);

    // Callbacks of later ABI revisions go HERE: the host checks structSize before reading them.
} ShardPluginCallbacks;

typedef struct ShardPluginInfo {
    uint32_t structSize;
    uint32_t abiMajor, abiMinor;
    ShardStr id;
    void*    user;
    const ShardPluginCallbacks* callbacks;
} ShardPluginInfo;

typedef ShardResult (SHARD_CALL *ShardPluginEntryFn)(const ShardHostApi* host, ShardPluginInfo* out);

#define SHARD_PLUGIN_ENTRY_NAME "shard_plugin_entry"

SHARD_PLUGIN_EXPORT ShardResult SHARD_CALL shard_plugin_entry(const ShardHostApi* host, ShardPluginInfo* out);

#ifdef __cplusplus
}
#endif

#endif

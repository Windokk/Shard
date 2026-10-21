/* Shard C ABI : what a plugin exports and what the host gives it. */
#ifndef SHARD_HOST_H
#define SHARD_HOST_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The host's table ----
 * One static table for every plugin; a plugin designates itself with the ShardPluginCtx it is handed in each callback.
 * The base is only what is needed to start; everything else is an interface fetched with query_interface, so this table barely grows. */

typedef struct ShardHostApi {
    uint32_t structSize;
    uint32_t abiMajor, abiMinor;
    ShardStr engineVersion;                                     /* "0.9.2", informative */

    /* thread : any */
    void        (SHARD_CALL *log)(ShardPluginCtx ctx, uint32_t level, ShardStr message);
    /* Memory counted against the plugin's budget (manifest budgets.memoryMB). Returns NULL on failure. What is allocated here is freed
     * with `free` here : never with the plugin's own allocator, and never by the host's. */
    void*       (SHARD_CALL *alloc)(ShardPluginCtx ctx, uint64_t size, uint64_t align);
    void        (SHARD_CALL *free)(ShardPluginCtx ctx, void* p);
    /* Looks an interface up by id ("shard.plugin"). `major` must match; the interface must be at least `minMinor`.
     * On success *out points to a struct that starts with `uint32_t structSize`, valid until the host exits.
     * SHARD_E_NOT_FOUND : the id is unknown, or this host does not offer it (an offline tool has no "shard.systems"). */
    ShardResult (SHARD_CALL *query_interface)(ShardStr id, uint32_t major, uint32_t minMinor, const void** out);
} ShardHostApi;

/* A world a module is told about. `name` is valid for the call. */
typedef struct ShardWorldInfo {
    uint32_t structSize;
    int32_t  assetId;                       /* the world's asset id in the project, -1 if it has none */
    ShardStr name;
} ShardWorldInfo;

/* ---- The plugin's callbacks ----
 * Same order and meaning as IPlugin. A callback returns SHARD_OK, or an error that makes the host fail the plugin (and the plugins that
 * depend on it) with the message the plugin logged. Every callback is optional : NULL means "nothing to do". All of them run on the main thread. */

typedef struct ShardPluginCallbacks {
    uint32_t structSize;
    ShardResult (SHARD_CALL *on_load)(void* user, ShardPluginCtx ctx);
    ShardResult (SHARD_CALL *on_initialize)(void* user, ShardPluginCtx ctx);
    ShardResult (SHARD_CALL *on_enable)(void* user, ShardPluginCtx ctx);
    ShardResult (SHARD_CALL *on_disable)(void* user, ShardPluginCtx ctx);
    ShardResult (SHARD_CALL *on_unload)(void* user, ShardPluginCtx ctx);

    /* Hot reload (shard_state.h). on_prepare_reload writes what must survive into `state` and returns SHARD_OK ; any other result (or no
     * callback) makes the plugin not reloadable. on_restore reads it back in the new binary, after on_enable. */
    ShardResult (SHARD_CALL *on_prepare_reload)(void* user, ShardPluginCtx ctx, ShardState state);
    ShardResult (SHARD_CALL *on_restore)(void* user, ShardPluginCtx ctx, ShardState state);

    /* Start and end of every frame while the plugin is enabled. Keep them short : their time is billed to the plugin. */
    ShardResult (SHARD_CALL *on_frame_begin)(void* user, ShardPluginCtx ctx, uint64_t frame);
    ShardResult (SHARD_CALL *on_frame_end)(void* user, ShardPluginCtx ctx, uint64_t frame);

    /* Modules : the content folder is mounted (mod://<id>/ is readable, shard_content.h), and what happens to the worlds */
    ShardResult (SHARD_CALL *on_content_mounted)(void* user, ShardPluginCtx ctx);
    ShardResult (SHARD_CALL *on_world_loaded)(void* user, ShardPluginCtx ctx, const ShardWorldInfo* world);
    ShardResult (SHARD_CALL *on_world_unloaded)(void* user, ShardPluginCtx ctx, const ShardWorldInfo* world);
    ShardResult (SHARD_CALL *on_play_start)(void* user, ShardPluginCtx ctx, const ShardWorldInfo* world);
    ShardResult (SHARD_CALL *on_play_stop)(void* user, ShardPluginCtx ctx, const ShardWorldInfo* world);

    /* Offline tools : the tool is about to run `task` ("cook", "bake", "validate") on the project file `project`. SHARD_OK lets it run ;
     * anything else cancels it, with *message (copied before the call returns) as the reason. Unlike the others, an error here does not
     * fail the plugin. */
    ShardResult (SHARD_CALL *on_tool_task)(void* user, ShardPluginCtx ctx, ShardStr task, ShardStr project, ShardStr* message);

    /* Callbacks of later ABI revisions go HERE : the host checks structSize before reading them. */
} ShardPluginCallbacks;

/* What the plugin fills in its entry point */
typedef struct ShardPluginInfo {
    uint32_t structSize;                    /* the plugin sets it to sizeof(ShardPluginInfo) of the headers it was built with ; the host reads no field past it */
    uint32_t abiMajor, abiMinor;            /* the plugin sets them to SHARD_ABI_MAJOR / SHARD_ABI_MINOR */
    ShardStr id;                            /* informative : the manifest is what the host trusts */
    void*    user;                          /* handed back to every callback */
    const ShardPluginCallbacks* callbacks;  /* must stay valid until on_unload returned */
} ShardPluginInfo;

/* The entry point of an ABI "c1" plugin (manifest "abi": "c1"). Called once, right after the library is loaded, on the main thread.
 * `out` is zeroed. Return SHARD_OK, or an error to refuse to load. */
typedef ShardResult (SHARD_CALL *ShardPluginEntryFn)(const ShardHostApi* host, ShardPluginInfo* out);

#define SHARD_PLUGIN_ENTRY_NAME "shard_plugin_entry"

SHARD_PLUGIN_EXPORT ShardResult SHARD_CALL shard_plugin_entry(const ShardHostApi* host, ShardPluginInfo* out);

#ifdef __cplusplus
}
#endif

#endif /* SHARD_HOST_H */

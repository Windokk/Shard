/* Interface "shard.content" 1.0 : the content folders of the loaded modules. Every module with content is mounted as mod://<id>/ ; the same
 * files form one layered tree by logical path ("materials/stone.mat"), later modules winning where they say they override (see the docs,
 * "Content"). Nothing here leaves the mounted folders. thread : any (the mounts change only while plugins load and unload, on the main thread). */
#ifndef SHARD_CONTENT_H
#define SHARD_CONTENT_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_CONTENT "shard.content"

/* Called once per file, with its path relative to the folder listed ('/' separators). Return SHARD_OK to go on. */
typedef ShardResult (SHARD_CALL *ShardListFn)(void* user, ShardStr path);

typedef struct ShardContentApi {
    uint32_t structSize;
    /* The mod:// address of a file of this plugin ("textures/a.png" -> "mod://<id>/textures/a.png"). Text out : copies at most `capacity`
     * bytes (no zero terminator) and stores the full length in *size ; `buffer` may be NULL to ask for the length. */
    ShardResult (SHARD_CALL *uri_of)(ShardPluginCtx ctx, ShardStr path, char* buffer, uint64_t capacity, uint64_t* size);
    /* The absolute file a mod:// address names. SHARD_E_NOT_FOUND : no such mount or file. Text out as uri_of. */
    ShardResult (SHARD_CALL *resolve)(ShardPluginCtx ctx, ShardStr uri, char* buffer, uint64_t capacity, uint64_t* size);
    /* The file that wins for a logical path across the layers, as a mod:// address. Text out as uri_of. */
    ShardResult (SHARD_CALL *resolve_logical)(ShardPluginCtx ctx, ShardStr logicalPath, char* buffer, uint64_t capacity, uint64_t* size);
    /* Reads a whole file (a mod:// address) into memory from the host's `alloc`, counted against the plugin's budget : free it with the
     * host's `free`. */
    ShardResult (SHARD_CALL *read)(ShardPluginCtx ctx, ShardStr uri, void** data, uint64_t* size);
    /* The files under `subdir` of a module (its id ; empty : this plugin), sorted. */
    ShardResult (SHARD_CALL *list)(ShardPluginCtx ctx, ShardStr pluginId, ShardStr subdir, ShardListFn fn, void* user);
} ShardContentApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_CONTENT_H */

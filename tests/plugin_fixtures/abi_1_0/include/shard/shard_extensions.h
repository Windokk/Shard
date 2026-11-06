#ifndef SHARD_EXTENSIONS_H
#define SHARD_EXTENSIONS_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_EXTENSIONS "shard.extensions"

#define SHARD_POINT_TOOL_VALIDATOR  "tool.validator"
#define SHARD_POINT_ASSET_TYPE      "asset.type"
#define SHARD_POINT_ASSET_IMPORTER  "asset.importer"

typedef struct ShardReport {
    uint32_t structSize;
    void* host;
    void (SHARD_CALL *add)(void* host, ShardStr line);
    void (SHARD_CALL *message)(void* host, ShardStr text);
} ShardReport;

typedef struct ShardValidatorItem {
    uint32_t structSize;
    ShardStr name;
    ShardResult (SHARD_CALL *validate)(void* user, ShardPluginCtx ctx, ShardStr projectFile, const ShardReport* report);
    void* user;
} ShardValidatorItem;

typedef struct ShardAssetTypeItem {
    uint32_t structSize;
    uint32_t extensionCount;
    ShardStr name;
    const ShardStr* extensions;
    // The returned pointer is the plugin's. The engine caches it and hands it back to `unload` when unused.
    void* (SHARD_CALL *load)(void* user, ShardPluginCtx ctx, ShardStr path, ShardStr file);
    void  (SHARD_CALL *unload)(void* user, ShardPluginCtx ctx, void* resource);
    void* user;
} ShardAssetTypeItem;

typedef struct ShardImporterItem {
    uint32_t structSize;
    uint32_t extensionCount;
    ShardStr name;
    const ShardStr* extensions;
    ShardResult (SHARD_CALL *import)(void* user, ShardPluginCtx ctx, ShardStr source, ShardStr outputDirectory, const ShardReport* report);
    void* user;
} ShardImporterItem;

typedef struct ShardExtensionsApi {
    uint32_t structSize;
    ShardResult (SHARD_CALL *add_validator)(ShardPluginCtx ctx, const ShardValidatorItem* item);
    ShardResult (SHARD_CALL *add_asset_type)(ShardPluginCtx ctx, const ShardAssetTypeItem* item);
    ShardResult (SHARD_CALL *add_importer)(ShardPluginCtx ctx, const ShardImporterItem* item);
    ShardResult (SHARD_CALL *remove)(ShardPluginCtx ctx, ShardStr point, ShardStr name);
    ShardResult (SHARD_CALL *get_asset)(ShardPluginCtx ctx, ShardStr typeName, ShardStr path, void** out);
} ShardExtensionsApi;

#ifdef __cplusplus
}
#endif

#endif

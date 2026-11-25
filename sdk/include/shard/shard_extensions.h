/* Interface "shard.extensions" 1.1 : what a plugin adds to the engine's subsystems through their extension points. The C ABI offers the
 * points whose items are only data and functions :
 *   "tool.validator"   a check the asset validator (ShardAssetValidator) runs on a project
 *   "asset.type"       a kind of asset : file extensions the project indexes, and the loader the resource cache calls
 *   "asset.importer"   a converter from a source format the engine does not read to files it does
 * Every item is copied by the host (strings and arrays included), named, and removed with the plugin. The functions run on the main thread. */
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

/* Where a validator or an importer writes what it found or made. `host` is handed back as is. Valid during the call only. */
typedef struct ShardReport {
    uint32_t structSize;
    void* host;
    /* A validator : one problem per call. An importer : a file it wrote (absolute path). */
    void (SHARD_CALL *add)(void* host, ShardStr line);
    /* An importer : why it failed, or a note on what it did. */
    void (SHARD_CALL *message)(void* host, ShardStr text);
} ShardReport;

typedef struct ShardValidatorItem {
    uint32_t structSize;
    ShardStr name;
    /* Checks the project (its project file) and adds a line to `report` for each problem. A result other than SHARD_OK is a problem too. */
    ShardResult (SHARD_CALL *validate)(void* user, ShardPluginCtx ctx, ShardStr projectFile, const ShardReport* report);
    void* user;
} ShardValidatorItem;

/* One flat shape of an asset type's icon, on a 24 x 24 grid, drawn in the type's accent colour. */
#define SHARD_ICON_RECT  0u     /* v : x0, y0, x1, y1 */
#define SHARD_ICON_DISC  1u     /* v : x, y, radius */
#define SHARD_ICON_POLY  2u     /* v : up to 4 points x, y (convex), pointCount of them */

typedef struct ShardIconShape {
    uint32_t kind;              /* SHARD_ICON_* */
    uint32_t pointCount;        /* SHARD_ICON_POLY : 3 or 4 */
    float alpha;                /* 0..1 : 1 for what matters, about 0.5 for the rest */
    float v[8];
} ShardIconShape;

typedef struct ShardAssetTypeItem {
    uint32_t structSize;
    uint32_t extensionCount;
    ShardStr name;
    const ShardStr* extensions;     /* ".foo" ; compared without case ; one the engine reads itself is not taken */
    /* Builds the resource from its file (`file` : absolute path, `path` : its path in the project). NULL : it cannot be loaded. The
     * pointer is the plugin's : the engine only keeps it in its cache and hands it back to `unload` once nobody uses it. */
    void* (SHARD_CALL *load)(void* user, ShardPluginCtx ctx, ShardStr path, ShardStr file);
    void  (SHARD_CALL *unload)(void* user, ShardPluginCtx ctx, void* resource);
    void* user;
    /* ---- 1.11 : how the content browser shows the files of the type. All optional, a descriptor without them is shown as a plain file ---- */
    ShardStr displayName;           /* "Post Profile" : what the browser calls the type. Empty : `name` */
    uint32_t accent;                /* 0xRRGGBB : the colour of the icon and the label. 0 : a colour derived from `name` */
    uint32_t iconCount;
    const ShardIconShape* icon;     /* iconCount shapes, copied during the call. NULL : a generic file icon in the accent */
} ShardAssetTypeItem;

typedef struct ShardImporterItem {
    uint32_t structSize;
    uint32_t extensionCount;
    ShardStr name;
    const ShardStr* extensions;     /* source extensions it takes (".foo") */
    /* Converts `source` into files in `outputDirectory` (it exists) ; report->add for each file written. SHARD_OK : it worked. */
    ShardResult (SHARD_CALL *import)(void* user, ShardPluginCtx ctx, ShardStr source, ShardStr outputDirectory, const ShardReport* report);
    void* user;
} ShardImporterItem;

typedef struct ShardExtensionsApi {
    uint32_t structSize;
    /* thread : main. SHARD_E_ALREADY_EXISTS : the point has an item of that name. */
    ShardResult (SHARD_CALL *add_validator)(ShardPluginCtx ctx, const ShardValidatorItem* item);
    ShardResult (SHARD_CALL *add_asset_type)(ShardPluginCtx ctx, const ShardAssetTypeItem* item);
    ShardResult (SHARD_CALL *add_importer)(ShardPluginCtx ctx, const ShardImporterItem* item);
    /* Removes one of this plugin's items. thread : main */
    ShardResult (SHARD_CALL *remove)(ShardPluginCtx ctx, ShardStr point, ShardStr name);
    /* The resource of one of this plugin's asset types (the item named `typeName`) at `path` in the project, loaded if it is not yet : what
     * its `load` returned. thread : main. SHARD_E_NOT_FOUND : unknown type or file, or it could not be loaded ; SHARD_E_UNSUPPORTED : this
     * host has no resource cache (an offline tool, a test). */
    ShardResult (SHARD_CALL *get_asset)(ShardPluginCtx ctx, ShardStr typeName, ShardStr path, void** out);
} ShardExtensionsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_EXTENSIONS_H */

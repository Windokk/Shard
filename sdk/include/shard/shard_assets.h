/* Interface "shard.assets" 1.0 : the assets of the project, by path and by id.
 *
 * An asset is a file of the project the engine knows (its asset database gives it an id : the value of a SHARD_FIELD_ASSET, shard_components.h)
 * under a path in the project ("models/crate.fbx"). shard.content is the other way in : the files a plugin ships in its own folder, as mod://
 * addresses. A path given here is a path in the project ; a mod:// address is also accepted wherever a file is read.
 *
 * What a plugin does with it : reads the geometry of the models to bake a navigation mesh or to cut the world into voxels, reads the pixels of
 * an image to pack an atlas, reads and writes the files it owns (a baked navigation mesh, a behaviour tree, a UI document, a voxel chunk) and
 * finds its own asset types' files by extension (shard.extensions' "asset.type" gives the loader ; get_asset gives the resource).
 *
 * A change to an asset on disk (the user saved it, the importer ran) is the hook "AssetImported" of shard.hooks, with the path as subject.
 *
 * thread : main, unless a function says otherwise. A plugin that writes the project declares the permission "fs.write:project" in its manifest,
 * one that reads outside its own folder "fs.read:project" (a native plugin is trusted whatever it declares, but the user is told). */
#ifndef SHARD_ASSETS_H
#define SHARD_ASSETS_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_ASSETS "shard.assets"

/* What kind of asset a file is. The engine's own kinds ; a plugin's asset type (shard.extensions) is SHARD_ASSET_PLUGIN, the type name is in
 * ShardAssetInfo::typeName. */
#define SHARD_ASSET_UNKNOWN     0u
#define SHARD_ASSET_MESH        1u
#define SHARD_ASSET_TEXTURE     2u
#define SHARD_ASSET_MATERIAL    3u
#define SHARD_ASSET_SHADER      4u
#define SHARD_ASSET_WORLD       5u
#define SHARD_ASSET_SOUND       6u
#define SHARD_ASSET_PREFAB      7u
#define SHARD_ASSET_PLUGIN      8u

typedef struct ShardAssetInfo {
    uint32_t structSize;
    int32_t  id;
    uint32_t kind;              /* SHARD_ASSET_* */
    uint32_t reserved;
    ShardStr path;              /* in the project, '/' separators. Valid for the call. */
    ShardStr typeName;          /* SHARD_ASSET_PLUGIN : the name of the asset type ; empty otherwise. Valid for the call. */
    uint64_t sizeBytes;         /* the file's size, 0 if it is not a file (an engine resource) */
    int64_t  modifiedTime;      /* the file's last write, seconds since 1970 (0 if unknown) */
} ShardAssetInfo;

/* Called once per asset. Return SHARD_OK to go on ; anything else stops the listing and is what `list` returns. */
typedef ShardResult (SHARD_CALL *ShardAssetListFn)(void* user, const ShardAssetInfo* asset);

/* The geometry of a mesh asset, for the code that bakes something from it. Triangles only, in the mesh's own space (apply the actor's world
 * transform yourself : shard.actors get_world_transform). */
typedef struct ShardMeshInfo {
    uint32_t structSize;
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t submeshCount;
    float    boundsMin[3];
    float    boundsMax[3];
} ShardMeshInfo;

typedef struct ShardMeshSlice {
    uint32_t indexOffset;       /* into the mesh's index array */
    uint32_t indexCount;
    uint32_t materialSlot;      /* the slot of the Model's material list this slice is drawn with */
} ShardMeshSlice;

/* A mesh read into the caller's arrays. Every array is optional (NULL : not wanted) and has the capacity next to it ; the counts come back in
 * `*Count` whatever the capacity (so a first call with capacities of 0 sizes the arrays of the second). Positions and normals are 3 floats a
 * vertex, uvs 2 (the first set), indices are triangle lists. */
typedef struct ShardMeshRead {
    uint32_t structSize;
    uint32_t vertexCapacity, indexCapacity, sliceCapacity;
    uint32_t vertexCount, indexCount, sliceCount;      /* out */
    float*          positions;
    float*          normals;
    float*          uvs;
    uint32_t*       indices;
    ShardMeshSlice* slices;
} ShardMeshRead;

typedef struct ShardAssetsApi {
    uint32_t structSize;

    /* ---- Finding ---- */
    /* The id of the asset at that path in the project (an engine resource's path included : "materials/default.mat"). SHARD_E_NOT_FOUND
     * otherwise. thread : any */
    ShardResult (SHARD_CALL *resolve)(ShardStr path, int32_t* id);
    /* What is known of an asset. The caller sets out->structSize. SHARD_E_NOT_FOUND : no such id. thread : any */
    ShardResult (SHARD_CALL *info)(int32_t id, ShardAssetInfo* out);
    /* The path of an asset, as the "text out" of the ABI. thread : any */
    ShardResult (SHARD_CALL *path_of)(int32_t id, char* buffer, uint64_t capacity, uint64_t* size);
    /* The assets under `subdir` of the project ("" : all) of that kind (SHARD_ASSET_UNKNOWN : any), in the order of their paths. */
    ShardResult (SHARD_CALL *list)(ShardStr subdir, uint32_t kind, ShardAssetListFn fn, void* user);

    /* ---- Files ---- */
    /* Reads a whole file (a path in the project, or a mod:// address) into memory from the host's `alloc`, counted against the plugin's
     * budget : free it with the host's `free`. thread : any */
    ShardResult (SHARD_CALL *read)(ShardPluginCtx ctx, ShardStr path, void** data, uint64_t* size);
    /* Writes a file in the project's resources (created or replaced, its folders made) and has the engine index it. *id receives its asset
     * id (may be NULL). A path that leaves the project ("..") is SHARD_E_INVALID_ARG. The file watcher then reports it like any other change.
     * thread : main */
    ShardResult (SHARD_CALL *write)(ShardPluginCtx ctx, ShardStr path, const void* data, uint64_t size, int32_t* id);
    /* SHARD_TRUE if the project has that file. thread : any */
    int32_t     (SHARD_CALL *exists)(ShardStr path);

    /* ---- Meshes ---- */
    /* thread : main (the mesh is read back from the GPU if the host does not keep a copy). SHARD_E_NOT_FOUND : not a mesh. */
    ShardResult (SHARD_CALL *mesh_info)(int32_t id, ShardMeshInfo* out);
    ShardResult (SHARD_CALL *mesh_read)(int32_t id, ShardMeshRead* io);

    /* ---- Images ---- */
    /* The size of an image asset, and the pixels decoded to 8 bit RGBA (4 bytes a pixel, rows from the top), up to `capacity` bytes of
     * them into `out` ; *size is the full size, width * height * 4 (call with capacity 0 to ask). A compressed image is decoded.
     * thread : any (a job may decode images). SHARD_E_NOT_FOUND : not an image. */
    ShardResult (SHARD_CALL *image_info)(int32_t id, uint32_t* width, uint32_t* height);
    ShardResult (SHARD_CALL *image_read)(int32_t id, uint8_t* out, uint64_t capacity, uint64_t* size);
} ShardAssetsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_ASSETS_H */

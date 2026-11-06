/* Interface "shard.render" 1.0 : what a plugin puts on the screen that the engine's components do not : meshes it builds, textures it fills,
 * sprites, instances and a 2D overlay.
 *
 * The engine draws from a snapshot of the simulation (Extract stage), not from the world, and so does this : a plugin hands the renderer
 * RESOURCES (a mesh, a texture, a material), and RETAINED things that use them (a render object, a batch of sprites, an overlay), which the
 * host copies into each frame's snapshot. Nothing here draws "now" : a call records, the frame draws. The GPU work happens on the render
 * thread ; the plugin never waits for it, and a handle can be used at once even if its upload is not done (what is not uploaded yet is not
 * drawn).
 *
 *   - voxels      : a Mesh per chunk (mesh_create from a job), a RenderObject per chunk, a texture array for the atlas
 *   - particles   : a SpriteBatch (sprite_batch_set every frame from a system), or an InstanceBatch of meshes
 *   - animation   : a skinned Mesh (JOINTS + WEIGHTS) and object_set_pose with the bone matrices every frame
 *   - runtime UI  : an Overlay (a callback that draws the UI's geometry into the frame), Geometry and Textures for what it draws, and
 *                   an Overlay on a render-target Texture to put a UI on a surface of the world
 *   - navigation, AI, networking : shard.debug draws their debug views
 *
 * Handles are 64 bit, owned by the plugin, 0 is never valid, and all of them are released with the plugin (disable or unload). Positions in the
 * world are the ABI's usual ones (shard_actors.h) ; a large world's positions are doubles. Matrices are 16 floats, column major (x axis first).
 * Colours are 8 bit r, g, b, a packed as r | g << 8 | b << 16 | a << 24, straight (not premultiplied) alpha.
 *
 * thread : any, for the functions that create, update or destroy a resource or a retained thing (they take a lock and record). thread : main
 * for load_*, the camera and the overlay's callback. A host without a renderer (a server, a tool, the tests) accepts every call, keeps the
 * handles and draws nothing : SHARD_E_UNSUPPORTED only for the functions that read the screen back (camera_info, world_to_screen...). */
#ifndef SHARD_RENDER_H
#define SHARD_RENDER_H

#include "shard_abi.h"
#include "shard_actors.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_RENDER "shard.render"

typedef uint64_t ShardMesh;
typedef uint64_t ShardTexture;
typedef uint64_t ShardMaterial;
typedef uint64_t ShardRenderObject;
typedef uint64_t ShardSpriteBatch;
typedef uint64_t ShardInstanceBatch;
typedef uint64_t ShardGeometry;
typedef uint64_t ShardOverlay;

/* ---- Meshes ---- */

/* Vertex attributes, in the order they are laid out in a vertex (interleaved, no padding). POSITION is always there. */
#define SHARD_VERTEX_POSITION   0x01u   /* float x3 */
#define SHARD_VERTEX_NORMAL     0x02u   /* float x3 */
#define SHARD_VERTEX_TANGENT    0x04u   /* float x4 : xyz and the handedness in w */
#define SHARD_VERTEX_UV0        0x08u   /* float x2 */
#define SHARD_VERTEX_UV1        0x10u   /* float x2 : the lightmap's coordinates, or a second texture's */
#define SHARD_VERTEX_COLOR      0x20u   /* uint8 x4, normalised */
#define SHARD_VERTEX_JOINTS     0x40u   /* uint16 x4 : the bones this vertex follows (skinning) */
#define SHARD_VERTEX_WEIGHTS    0x80u   /* float x4 : how much each of them, summing to 1 (skinning) */

#define SHARD_MESH_INDEX16      1u      /* the indices are uint16_t, not uint32_t */
#define SHARD_MESH_DYNAMIC      2u      /* updated often (every frame, or nearly) : the host picks storage that suits */

typedef struct ShardSubmesh {
    uint32_t indexOffset;
    uint32_t indexCount;
    uint32_t materialSlot;      /* the slot of the object's material list this part is drawn with */
} ShardSubmesh;

typedef struct ShardMeshDesc {
    uint32_t structSize;
    uint32_t vertexFlags;       /* SHARD_VERTEX_* */
    uint32_t vertexCount;
    uint32_t indexCount;        /* a multiple of 3 : triangle lists */
    uint32_t submeshCount;      /* 0 : one part made of all the indices, in slot 0 */
    uint32_t flags;             /* SHARD_MESH_* */
    const void* vertices;       /* vertexCount vertices as the flags lay them out. Copied before the call returns. */
    const void* indices;        /* indexCount uint32_t (uint16_t with SHARD_MESH_INDEX16). Copied. */
    const ShardSubmesh* submeshes;
    float boundsMin[3];         /* the box the frustum culling uses ; both zero : the host computes it from the positions */
    float boundsMax[3];
    ShardStr name;              /* for the logs and the profiler, optional */
} ShardMeshDesc;

/* The area of the vertex array an update replaces */
typedef struct ShardMeshRange {
    uint32_t structSize;
    uint32_t firstVertex;
    uint32_t vertexCount;
    uint32_t reserved;
    const void* vertices;       /* vertexCount vertices in the mesh's layout. Copied. */
} ShardMeshRange;

/* ---- Textures ---- */

#define SHARD_TEXTURE_R8        1u
#define SHARD_TEXTURE_RG8       2u
#define SHARD_TEXTURE_RGBA8     3u
#define SHARD_TEXTURE_R16F      4u
#define SHARD_TEXTURE_RGBA16F   5u
#define SHARD_TEXTURE_R32F      6u

#define SHARD_TEXTURE_SRGB          0x01u   /* RGBA8 only : the colours are in the sRGB curve (an albedo, a UI image), the GPU converts when sampling */
#define SHARD_TEXTURE_MIPS          0x02u   /* a mip chain is generated (and regenerated by update_texture) */
#define SHARD_TEXTURE_NEAREST       0x04u   /* no filtering : pixel art, a voxel atlas */
#define SHARD_TEXTURE_CLAMP         0x08u   /* clamp to the edge instead of repeating */
#define SHARD_TEXTURE_RENDER_TARGET 0x10u   /* an overlay can draw into it (add_overlay with this target), and a material can sample it */

typedef struct ShardTextureDesc {
    uint32_t structSize;
    uint32_t width, height;
    uint32_t layers;            /* 0 or 1 : a 2D texture ; more : a 2D array of that many layers (a voxel atlas) -- SHARD_E_UNSUPPORTED on a host that has no texture arrays (this one) : use one big 2D atlas */
    uint32_t format;            /* SHARD_TEXTURE_R8... */
    uint32_t flags;             /* SHARD_TEXTURE_* */
    const void* pixels;         /* NULL : zeroes (transparent black). Otherwise width * height * layers texels, rows from the top, copied before returning. */
    ShardStr name;              /* optional */
} ShardTextureDesc;

typedef struct ShardTextureRegion {
    uint32_t structSize;
    uint32_t x, y, layer;
    uint32_t width, height;
    uint32_t rowPitch;          /* bytes between two rows of `pixels`, 0 : tightly packed */
    uint32_t reserved;
    const void* pixels;         /* copied before the call returns */
} ShardTextureRegion;

/* ---- Render objects ---- */

#define SHARD_OBJECT_CAST_SHADOWS   0x01u   /* default on in the C++ SDK */
#define SHARD_OBJECT_HIDDEN         0x02u
#define SHARD_OBJECT_STATIC         0x04u   /* never moves : the renderer may bake lighting for it */

/* A pose in the world. rotation is a quaternion x, y, z, w. */
typedef struct ShardRenderTransform {
    double position[3];
    float  rotation[4];
    float  scale[3];
} ShardRenderTransform;

typedef struct ShardRenderObjectDesc {
    uint32_t structSize;
    uint32_t flags;             /* SHARD_OBJECT_* */
    ShardMesh mesh;
    const ShardMaterial* materials;     /* one per material slot of the mesh, copied. A missing one is the engine's default material. */
    uint32_t materialCount;
    uint32_t reserved;
    ShardActor pickActor;       /* the actor the editor selects when this is clicked, outlined when selected. 0 : not pickable. */
    ShardRenderTransform transform;
} ShardRenderObjectDesc;

/* ---- Sprite batches (particles, flares, impostors) ---- */

#define SHARD_SPRITE_FACE_CAMERA    0u  /* the quad turns to the camera */
#define SHARD_SPRITE_FACE_AXIS      1u  /* it turns around ShardSprite::axis to the camera (a grass blade, a beam, a stretched spark) */
#define SHARD_SPRITE_FACE_FIXED     2u  /* it lies in the plane perpendicular to ShardSprite::axis (a decal-like ground splash) */

#define SHARD_BLEND_OPAQUE          0u
#define SHARD_BLEND_CUTOUT          1u  /* alpha below 0.5 is dropped */
#define SHARD_BLEND_ALPHA           2u
#define SHARD_BLEND_ADDITIVE        3u
#define SHARD_BLEND_PREMULTIPLIED   4u

#define SHARD_SPRITE_SORT_NONE      0u
#define SHARD_SPRITE_SORT_FAR_FIRST 1u  /* the host sorts the sprites by distance to the camera each frame (alpha blending needs it) */

#define SHARD_SPRITE_DEPTH_TEST     0x01u   /* hidden by the geometry in front of it (default on in the C++ SDK) */
#define SHARD_SPRITE_DEPTH_WRITE    0x02u
#define SHARD_SPRITE_SCREEN_SIZE    0x04u   /* `size` is in pixels, not world units */
#define SHARD_SPRITE_SOFT           0x08u   /* fades out near the geometry behind it, where the host can */
#define SHARD_SPRITE_HIDDEN         0x10u

typedef struct ShardSprite {
    float    position[3];       /* in the world, relative to the batch's origin */
    float    rotation;          /* radians, around the direction the quad faces */
    float    size[2];           /* width, height */
    uint32_t color;             /* multiplies the texture */
    float    uvRect[4];         /* u0, v0, u1, v1 : the part of the texture it shows (a flipbook frame) */
    float    axis[3];           /* SHARD_SPRITE_FACE_AXIS / FIXED : the direction, not necessarily unit ; otherwise ignored */
    float    custom;            /* free : reaches the material as `sprite_custom` (a particle's age, 0 to 1, for a gradient or a flipbook blend) */
} ShardSprite;

typedef struct ShardSpriteBatchDesc {
    uint32_t structSize;
    uint32_t facing;            /* SHARD_SPRITE_FACE_* */
    uint32_t blend;             /* SHARD_BLEND_* */
    uint32_t sort;              /* SHARD_SPRITE_SORT_* */
    uint32_t flags;             /* SHARD_SPRITE_* */
    uint32_t maxSprites;        /* the most one set will hold (the host reserves for it) */
    ShardTexture texture;       /* 0 : a white texel */
    ShardMaterial material;     /* reserved : a material that would replace the shading of the sprites (a distortion, a custom shader) ; ignored for now */
    double origin[3];           /* the sprites' positions are relative to it : keeps float precision in a large world */
} ShardSpriteBatchDesc;

/* ---- Instance batches (debris, grass, props) ---- */

typedef struct ShardInstance {
    float position[3];          /* relative to the batch's origin */
    float rotation[4];          /* quaternion x, y, z, w */
    float scale[3];
} ShardInstance;

typedef struct ShardInstanceBatchDesc {
    uint32_t structSize;
    uint32_t flags;             /* SHARD_OBJECT_* */
    ShardMesh mesh;
    const ShardMaterial* materials;
    uint32_t materialCount;
    uint32_t maxInstances;
    double origin[3];
} ShardInstanceBatchDesc;

/* ---- The 2D overlay ---- */

/* The vertex of 2D geometry : the layout of a UI library's vertices (RmlUi, Dear ImGui's draw lists...). position is in pixels of the target,
 * from its top left corner ; the colour multiplies the texture. */
typedef struct ShardVertex2D {
    float    position[2];
    uint32_t color;
    float    uv[2];
} ShardVertex2D;

#define SHARD_CLIP_DISABLE      0u      /* no mask : what follows is not clipped by one */
#define SHARD_CLIP_SET          1u      /* the mask becomes the geometry's area */
#define SHARD_CLIP_SET_INVERSE  2u      /* the mask becomes everything but the geometry's area */
#define SHARD_CLIP_INTERSECT    3u      /* the mask is cut down to the geometry's area */

/* What the overlay's callback draws with : records commands into the frame. Valid during the callback only. The commands draw in the order
 * they were given, over what the 3D scene drew (and over earlier overlays). */
typedef struct ShardDrawApi {
    uint32_t structSize;
    /* Draws a geometry, moved by `translation` pixels, with `texture` (0 : plain colour). The blending is PREMULTIPLIED alpha (source * 1 + destination
     * * (1 - source alpha)) : the colours of the vertices and the texels of the textures are expected premultiplied, as a UI library such as RmlUi
     * produces them. A texture_load image is straight alpha : premultiply it, or load it yourself and use texture_create. */
    void (SHARD_CALL *draw_geometry)(ShardGeometry geometry, ShardTexture texture, const float translation[2]);
    /* The transform applied to what follows, 16 floats column major, in pixels ; NULL : none. */
    void (SHARD_CALL *set_transform)(const float matrix[16]);
    /* The rectangle (x, y from the top left, width, height, in pixels) nothing is drawn outside of, while `enabled`. */
    void (SHARD_CALL *set_scissor)(int32_t enabled, int32_t x, int32_t y, int32_t width, int32_t height);
    /* A mask by shape (SHARD_CLIP_*) : what follows is drawn only inside the area the geometry covers (rounded corners, rotated clips). A host
     * without a stencil buffer (this one, for the viewport) clips to the bounding rectangle of the shape, and SHARD_CLIP_SET_INVERSE does not clip. */
    void (SHARD_CALL *set_clip_mask)(uint32_t operation, ShardGeometry geometry, const float translation[2]);
    /* Fills the target with a colour (r, g, b, a floats in 0..1) : the first thing an overlay on a render-target texture does. */
    void (SHARD_CALL *clear)(const float rgba[4]);
    /* Geometry for this frame only (a debug overlay, a one-off quad) : copied, no handle to manage. */
    void (SHARD_CALL *draw_triangles)(const ShardVertex2D* vertices, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount, ShardTexture texture);
} ShardDrawApi;

/* What an overlay is told when it is asked to draw */
typedef struct ShardOverlayFrame {
    uint32_t structSize;
    uint32_t width, height;     /* the target's size in pixels : the viewport, or the render-target texture */
    float    dpiScale;          /* 1 on a normal display, 2 on a "retina" one... */
    float    deltaTime;         /* real seconds since the last frame (not scaled by the time scale : a UI animates while the game is paused) */
    uint64_t frame;
    int32_t  playing;           /* SHARD_TRUE in play mode */
    float    originX, originY;  /* where the target's top left corner is in the window, in pixels : subtract it from the positions of shard.input's events to get
                                 * positions in the target. 0, 0 in a player and for a render-target texture. */
} ShardOverlayFrame;

/* thread : main. Called once per frame for each overlay, after the simulation, before the frame is handed to the renderer. */
typedef void (SHARD_CALL *ShardOverlayFn)(void* user, ShardPluginCtx ctx, const ShardOverlayFrame* frame, const ShardDrawApi* draw);

#define SHARD_OVERLAY_IN_EDITOR     0x01u   /* also draws in the editor's viewport outside play mode (a UI being designed) ; otherwise only while playing */

typedef struct ShardOverlayDesc {
    uint32_t structSize;
    uint32_t flags;             /* SHARD_OVERLAY_* */
    int32_t  order;             /* the overlays draw by increasing order, then in the order they were added */
    uint32_t reserved;
    ShardStr name;
    ShardTexture target;        /* 0 : the screen (the game's viewport). A SHARD_TEXTURE_RENDER_TARGET texture : drawn into it instead. */
    ShardOverlayFn draw;
    void* user;
} ShardOverlayDesc;

/* ---- The camera ---- */

typedef struct ShardCameraInfo {
    uint32_t structSize;
    int32_t  valid;             /* SHARD_FALSE : the world has no usable camera */
    uint32_t width, height;     /* of the viewport, in pixels */
    int32_t  orthographic;
    float    fovDegrees;        /* vertical, perspective only */
    float    orthoSize;
    float    nearPlane, farPlane;
    double   position[3];
    float    rotation[4];       /* quaternion x, y, z, w */
    float    view[16];          /* relative to `position` : positions are brought to the camera before they are multiplied, so a large world keeps its precision */
    float    projection[16];
} ShardCameraInfo;

typedef struct ShardRenderApi {
    uint32_t structSize;

    /* ---- Meshes ---- */
    /* SHARD_E_INVALID_ARG : an inconsistent description (an index out of range, a skinned vertex without both JOINTS and WEIGHTS, a count
     * that is not a multiple of 3...) ; the log says which. SHARD_E_OUT_OF_MEMORY : over the plugin's budget. thread : any */
    ShardResult (SHARD_CALL *mesh_create)(ShardPluginCtx ctx, const ShardMeshDesc* desc, ShardMesh* out);
    /* Replaces the whole content (the vertex flags may change ; objects that use the mesh follow). thread : any */
    ShardResult (SHARD_CALL *mesh_update)(ShardPluginCtx ctx, ShardMesh mesh, const ShardMeshDesc* desc);
    /* Replaces some vertices, same layout (soft bodies, a morph target applied on the CPU). thread : any */
    ShardResult (SHARD_CALL *mesh_update_range)(ShardPluginCtx ctx, ShardMesh mesh, const ShardMeshRange* range);
    /* A model asset of the project as a mesh the plugin can use in its own objects. thread : main */
    ShardResult (SHARD_CALL *mesh_from_asset)(ShardPluginCtx ctx, int32_t assetId, ShardMesh* out);
    ShardResult (SHARD_CALL *mesh_destroy)(ShardPluginCtx ctx, ShardMesh mesh);

    /* ---- Textures ---- */
    ShardResult (SHARD_CALL *texture_create)(ShardPluginCtx ctx, const ShardTextureDesc* desc, ShardTexture* out);
    ShardResult (SHARD_CALL *texture_update)(ShardPluginCtx ctx, ShardTexture texture, const ShardTextureRegion* region);
    /* An image file of the project or a mod:// address, decoded on a worker : the handle is usable at once, and shows nothing until the image
     * is there (texture_size tells). thread : main */
    ShardResult (SHARD_CALL *texture_load)(ShardPluginCtx ctx, ShardStr path, uint32_t flags, ShardTexture* out);
    ShardResult (SHARD_CALL *texture_from_asset)(ShardPluginCtx ctx, int32_t assetId, ShardTexture* out);
    /* The size once known (0, 0 while a loaded image is not decoded yet). thread : any */
    ShardResult (SHARD_CALL *texture_size)(ShardPluginCtx ctx, ShardTexture texture, uint32_t* width, uint32_t* height);
    ShardResult (SHARD_CALL *texture_destroy)(ShardPluginCtx ctx, ShardTexture texture);

    /* ---- Materials ---- */
    /* A private copy of a material asset (a path in the project or a mod:// address) : its parameters can be set without touching the
     * asset's other users. thread : main */
    ShardResult (SHARD_CALL *material_load)(ShardPluginCtx ctx, ShardStr path, ShardMaterial* out);
    /* A parameter by the name the material's shader declares. SHARD_E_NOT_FOUND : no such parameter. thread : any */
    ShardResult (SHARD_CALL *material_set_float)(ShardPluginCtx ctx, ShardMaterial material, ShardStr name, float value);
    ShardResult (SHARD_CALL *material_set_vec4)(ShardPluginCtx ctx, ShardMaterial material, ShardStr name, const float value[4]);
    ShardResult (SHARD_CALL *material_set_texture)(ShardPluginCtx ctx, ShardMaterial material, ShardStr name, ShardTexture texture);
    ShardResult (SHARD_CALL *material_destroy)(ShardPluginCtx ctx, ShardMaterial material);

    /* ---- Render objects ---- */
    ShardResult (SHARD_CALL *object_create)(ShardPluginCtx ctx, const ShardRenderObjectDesc* desc, ShardRenderObject* out);
    ShardResult (SHARD_CALL *object_set_transform)(ShardPluginCtx ctx, ShardRenderObject object, const ShardRenderTransform* transform);
    ShardResult (SHARD_CALL *object_set_mesh)(ShardPluginCtx ctx, ShardRenderObject object, ShardMesh mesh);
    ShardResult (SHARD_CALL *object_set_materials)(ShardPluginCtx ctx, ShardRenderObject object, const ShardMaterial* materials, uint32_t count);
    ShardResult (SHARD_CALL *object_set_flags)(ShardPluginCtx ctx, ShardRenderObject object, uint32_t flags);
    /* The pose of a skinned mesh : `count` matrices of 16 floats, one per bone (the bone's transform in the mesh's space times its inverse
     * bind pose), copied. The vertices follow the bones their JOINTS name. SHARD_E_INVALID_ARG : the mesh has no JOINTS and WEIGHTS. */
    ShardResult (SHARD_CALL *object_set_pose)(ShardPluginCtx ctx, ShardRenderObject object, const float* matrices, uint32_t count);
    ShardResult (SHARD_CALL *object_destroy)(ShardPluginCtx ctx, ShardRenderObject object);

    /* ---- Sprite and instance batches ---- */
    ShardResult (SHARD_CALL *sprite_batch_create)(ShardPluginCtx ctx, const ShardSpriteBatchDesc* desc, ShardSpriteBatch* out);
    /* Replaces the batch's sprites (copied ; more than maxSprites are dropped, the rest drawn). Call it every frame from the system that
     * simulates them ; the last set stays on screen until the next. thread : any */
    ShardResult (SHARD_CALL *sprite_batch_set)(ShardPluginCtx ctx, ShardSpriteBatch batch, const ShardSprite* sprites, uint32_t count);
    ShardResult (SHARD_CALL *sprite_batch_set_flags)(ShardPluginCtx ctx, ShardSpriteBatch batch, uint32_t flags);
    ShardResult (SHARD_CALL *sprite_batch_destroy)(ShardPluginCtx ctx, ShardSpriteBatch batch);
    ShardResult (SHARD_CALL *instance_batch_create)(ShardPluginCtx ctx, const ShardInstanceBatchDesc* desc, ShardInstanceBatch* out);
    ShardResult (SHARD_CALL *instance_batch_set)(ShardPluginCtx ctx, ShardInstanceBatch batch, const ShardInstance* instances, uint32_t count);
    ShardResult (SHARD_CALL *instance_batch_destroy)(ShardPluginCtx ctx, ShardInstanceBatch batch);

    /* ---- The overlay ---- */
    /* thread : main */
    ShardResult (SHARD_CALL *overlay_add)(ShardPluginCtx ctx, const ShardOverlayDesc* desc, ShardOverlay* out);
    ShardResult (SHARD_CALL *overlay_remove)(ShardPluginCtx ctx, ShardOverlay overlay);
    /* Geometry the overlay draws again and again (a UI library compiles its elements once). Copied. thread : any */
    ShardResult (SHARD_CALL *geometry_create)(ShardPluginCtx ctx, const ShardVertex2D* vertices, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount, ShardGeometry* out);
    ShardResult (SHARD_CALL *geometry_destroy)(ShardPluginCtx ctx, ShardGeometry geometry);

    /* ---- The camera ---- */
    /* The active camera of the game's viewport. The caller sets out->structSize. thread : main or script. SHARD_E_UNSUPPORTED : no renderer. */
    ShardResult (SHARD_CALL *camera_info)(ShardCameraInfo* out);
    /* A point of the world as the camera sees it : x, y in pixels of the viewport from its top left, z the distance along the view axis
     * (negative : behind the camera). */
    ShardResult (SHARD_CALL *world_to_screen)(const double position[3], float out[3]);
    /* The ray through a pixel of the viewport : an origin in the world and a unit direction. */
    ShardResult (SHARD_CALL *screen_to_ray)(float x, float y, double origin[3], float direction[3]);
} ShardRenderApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_RENDER_H */

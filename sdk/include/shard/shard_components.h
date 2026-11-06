/* Interface "shard.components" 1.4 : component types described field by field.
 *
 * One registration gives a type two faces, under one name ("<plugin id>.<name>", made by the host) :
 *   - an ECS component : a block of `size` bytes on an entity, what systems query and write (shard_ecs.h). Plain data : copied with memcpy,
 *     never constructed nor destroyed, so no pointer to memory the plugin owns. What does not fit in the block goes in a blob (1.1, below) ;
 *   - an actor component : what a world file saves, the editor shows in its inspector and an actor carries. Its fields are the ones described
 *     here, read and written at their offset in the block.
 * An actor that has the actor component has the ECS component on its entity, with the same values : the actor component writes its values to
 * the entity when it is loaded, edited or attached, and reads them back from the entity when the world is saved or copied. So what a system
 * changed is what gets saved. An entity a plugin spawned on its own (shard_ecs.h, cmd_spawn) has no actor : it is not saved.
 *
 * The type lives as long as the plugin is enabled. Its ECS id stays reserved for the whole run : a plugin enabled again gets the same
 * ShardComponentType back, provided it describes the same size (a type whose size changed needs a restart of the engine).
 *
 * Blobs (1.1) : bytes the host keeps outside the block, as many as needed (a chunk of voxels), counted against the plugin's memory budget.
 * A field SHARD_FIELD_BLOB holds the ShardBlob handle. The plugin owns its blobs : blob_free gives one back, and all of them go with the
 * plugin (disable, unload). The block is still copied with memcpy, so two blocks may hold the same handle : that is a shared blob, not a
 * copy. The actor component is the exception, it owns the blobs of its blob fields : it saves their bytes with the world (base64), a copy of
 * the actor (play mode) gets copies of them, and removing the component frees them. A system that puts a new blob in such a field frees
 * the one it replaces.
 *
 * Text and assets (1.2) : a field SHARD_FIELD_STRING holds a ShardBlob too, whose bytes are the UTF-8 text (no terminator) : the actor
 * component owns it like any blob, but the inspector shows it as a text box and the world file saves it as a JSON string. string_set and
 * string_get read and write it without touching the blob by hand. A field SHARD_FIELD_ASSET is an int32_t, the id of an asset of the project
 * (0 : none) : the inspector shows an asset picker, the world file saves the asset by its name, and the engine knows the component uses it.
 *
 * Life cycle (1.4) : a type may ask to be told when one of its components joins an actor, leaves it, or has a field edited (ShardComponentCallbacks).
 * That is where a plugin makes what the component stands for -- a render object for a chunk of voxels, a navigation agent, a particle emitter's
 * state -- and drops it, without scanning the ECS every frame to find out what appeared. */
#ifndef SHARD_COMPONENTS_H
#define SHARD_COMPONENTS_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_COMPONENTS "shard.components"

typedef uint64_t ShardComponentType;    /* 0 is never valid */
typedef uint64_t ShardBlob;             /* 0 is never valid : an empty blob field */

/* Field types : the C type the field has in the block, and how it is saved */
#define SHARD_FIELD_F32     1u      /* float */
#define SHARD_FIELD_F64     2u      /* double */
#define SHARD_FIELD_I32     3u      /* int32_t */
#define SHARD_FIELD_U32     4u      /* uint32_t */
#define SHARD_FIELD_I64     5u      /* int64_t */
#define SHARD_FIELD_U64     6u      /* uint64_t */
#define SHARD_FIELD_BOOL    7u      /* uint8_t, 0 or 1 */
#define SHARD_FIELD_VEC2    8u      /* float[2] */
#define SHARD_FIELD_VEC3    9u      /* float[3] */
#define SHARD_FIELD_VEC4    10u     /* float[4] */
#define SHARD_FIELD_QUAT    11u     /* float[4] : x, y, z, w */
#define SHARD_FIELD_COLOR   12u     /* float[4] : r, g, b, a (the inspector shows a colour picker) */
#define SHARD_FIELD_BLOB    13u     /* ShardBlob (uint64_t) : saved as the blob's bytes, not shown by the inspector (1.1) */
#define SHARD_FIELD_STRING  14u     /* ShardBlob (uint64_t) holding UTF-8 text : edited in the inspector, saved as text (1.2). 8 per component at most */
#define SHARD_FIELD_ASSET   15u     /* int32_t : the id of an asset, 0 for none (1.2) */
#define SHARD_FIELD_ACTOR   16u     /* ShardActor (uint64_t) : another actor, saved by its path in the world. Scripts only (1.5) */

/* Field flags. Without any, a field is saved and editable in the inspector. */
#define SHARD_FIELD_RUNTIME     1u  /* runtime state : shown by the inspector, read only, not saved */
#define SHARD_FIELD_PRIVATE     2u  /* neither shown nor saved : the block keeps it, nothing else sees it (not for a string : use a blob) */
#define SHARD_FIELD_ACCESSOR    4u  /* a SHARD_FIELD_STRING of a script that is not a handle : the plugin's own string at that offset, read and written
                                     * by the script's text_get and text_set callbacks (shard_scripts.h) instead of a blob (1.5) */

typedef struct ShardFieldDesc {
    uint32_t structSize;
    uint32_t type;          /* SHARD_FIELD_* */
    ShardStr name;          /* letters, digits and '_' ; "type", "active" and "tags" are reserved */
    uint32_t offset;        /* in the block, aligned for its type */
    uint32_t flags;         /* SHARD_FIELD_RUNTIME... */
    float    min, max;      /* bounds of the inspector's widget ; min == max : none */
    ShardStr tooltip;       /* optional */
} ShardFieldDesc;

/* Told about the actor components of a type (1.4). `actor` is the ShardActor that carries it, `entity` its ECS entity, `block` the component's block
 * (valid for the call). All of them run on the main thread, outside the systems, in the editor's edit mode as well as in play mode, with the
 * values the world file or the inspector gave already in the block.
 *   on_added   : the component became live : its entity exists and its values are in the block. Once per activation (added to an actor that is in
 *                a world, loaded from a file, copied for the play mode, enabled again after being disabled).
 *   on_removed : it is about to stop being live (removed, disabled, its actor destroyed, the world unloaded) ; the block is still intact. A
 *                destroyed actor's handle is still valid in this call.
 *   on_changed : a field was edited (the inspector, shard.actors' set_component...) ; `field` is its index in the description's `fields`. A system
 *                writing the block does NOT call it. */
typedef void (SHARD_CALL *ShardComponentFn)(void* user, ShardPluginCtx ctx, uint64_t actor, uint64_t entity, void* block);
typedef void (SHARD_CALL *ShardComponentChangedFn)(void* user, ShardPluginCtx ctx, uint64_t actor, uint64_t entity, void* block, uint32_t field);

typedef struct ShardComponentCallbacks {
    uint32_t structSize;
    uint32_t reserved;
    ShardComponentFn        on_added;
    ShardComponentFn        on_removed;
    ShardComponentChangedFn on_changed;
    void* user;                     /* handed back to every callback */
} ShardComponentCallbacks;

typedef struct ShardComponentDesc {
    uint32_t structSize;
    uint32_t size;                  /* bytes of the block, 1 to SHARD_COMPONENT_MAX_SIZE */
    uint32_t align;                 /* a power of two, 16 at most */
    uint32_t fieldCount;            /* SHARD_COMPONENT_MAX_FIELDS at most */
    ShardStr name;                  /* "Spin" : the host registers "<plugin id>.Spin" */
    const ShardFieldDesc* fields;   /* read during the call only */
    const void* defaults;           /* `size` bytes : the value of a new component. NULL : zeroes. Read during the call only. */
    /* ---- 1.4 ---- */
    const ShardComponentCallbacks* callbacks;   /* optional, copied up to its structSize during the call */
} ShardComponentDesc;

#define SHARD_COMPONENT_MAX_SIZE    1024u
#define SHARD_COMPONENT_MAX_FIELDS  64u
#define SHARD_COMPONENT_MAX_STRINGS 8u      /* SHARD_FIELD_STRING and SHARD_FIELD_ACTOR fields of one component or script together (1.2) */

typedef struct ShardComponentsApi {
    uint32_t structSize;

    /* Registers a type, from on_load. thread : main.
     * SHARD_E_INVALID_ARG : a field out of the block, misaligned, overlapping another, of an unknown type, a bad name, a size or an alignment
     * out of bounds (the log says which). SHARD_E_ALREADY_EXISTS : the name is taken (by this plugin, or by a type of another size). */
    ShardResult (SHARD_CALL *register_type)(ShardPluginCtx ctx, const ShardComponentDesc* desc, ShardComponentType* out);

    /* The type of that full name ("com.acme.weather.Cloud") : one of a C plugin, or one of the engine's that shard_ecs.h documents
     * ("shard.LocalTransform"...). thread : any. SHARD_E_NOT_FOUND if there is none. */
    ShardResult (SHARD_CALL *find_type)(ShardPluginCtx ctx, ShardStr name, ShardComponentType* out);

    /* Size and alignment of the block. thread : any */
    ShardResult (SHARD_CALL *type_layout)(ShardPluginCtx ctx, ShardComponentType type, uint32_t* size, uint32_t* align);

    /* The fields of a type a C plugin registered (another plugin's, to read its blocks without its header). `out->structSize` must be set
     * by the caller ; the strings in *out stay valid while that type is registered. thread : any.
     * SHARD_E_NOT_FOUND : no such field / index. */
    ShardResult (SHARD_CALL *field_count)(ShardPluginCtx ctx, ShardComponentType type, uint32_t* count);
    ShardResult (SHARD_CALL *field_at)(ShardPluginCtx ctx, ShardComponentType type, uint32_t index, ShardFieldDesc* out);
    ShardResult (SHARD_CALL *find_field)(ShardPluginCtx ctx, ShardComponentType type, ShardStr name, ShardFieldDesc* out);

    /* ---- 1.1 : blobs ---- */
    /* A blob of `size` bytes, zeroed (size may be 0). thread : any. SHARD_E_OUT_OF_MEMORY : over the plugin's enforced budget. */
    ShardResult (SHARD_CALL *blob_create)(ShardPluginCtx ctx, uint64_t size, ShardBlob* out);
    /* Its bytes and size. The pointer stays valid until the blob is resized or freed ; reading and writing them from several threads at
     * once is the plugin's to order (a system that writes a blob declares the component that holds it in its writes). thread : any.
     * SHARD_E_STALE_HANDLE : freed, or not this plugin's. */
    ShardResult (SHARD_CALL *blob_data)(ShardPluginCtx ctx, ShardBlob blob, void** data, uint64_t* size);
    /* New size, the first bytes kept, the rest zeroed ; the bytes move. thread : any */
    ShardResult (SHARD_CALL *blob_resize)(ShardPluginCtx ctx, ShardBlob blob, uint64_t size);
    ShardResult (SHARD_CALL *blob_free)(ShardPluginCtx ctx, ShardBlob blob);

    /* ---- 1.2 : text ---- */
    /* Puts `text` (copied now) in the string field `*field` points to : in its blob if it holds a live one, else in a new blob whose handle
     * is stored in *field. thread : any. SHARD_E_OUT_OF_MEMORY : over the plugin's enforced budget. */
    ShardResult (SHARD_CALL *string_set)(ShardPluginCtx ctx, ShardBlob* field, ShardStr text);
    /* The text of a string field (empty for blob 0). `out->data` stays valid until the field is set again or freed. thread : any.
     * SHARD_E_STALE_HANDLE : freed, or not this plugin's. */
    ShardResult (SHARD_CALL *string_get)(ShardPluginCtx ctx, ShardBlob blob, ShardStr* out);
} ShardComponentsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_COMPONENTS_H */

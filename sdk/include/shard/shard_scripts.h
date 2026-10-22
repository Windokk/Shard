/* Interface "shard.scripts" 1.1 : scripts, the components that carry a plugin's gameplay code.
 *
 * A script is an object of the plugin (a C++ class in the SDK) that lives on an actor, as the engine's own Script components do. The host
 * owns its memory and calls it back : it is created with the component, receives the callbacks of the actor's life and of the frame
 * (play, tick, fixed tick, late tick, stop...), in the order of the scripts of the world, and goes with the actor. Its fields are shown by
 * the inspector, saved with the world and copied with the actor (play mode copies the world), exactly as for a component of
 * shard_components.h : the same ShardFieldDesc, the same field types (text, assets, blobs included), at offsets in the object.
 *
 * Unlike a component's block, the object is NOT plain data : it is constructed in place in storage the host provides (`size` bytes, aligned
 * on `align`), and destructed there. It is never moved or copied by the host with memcpy. A copy of the actor (play mode) constructs a
 * new object and copies the described fields into it : what is not a field starts fresh, as if the actor was just created.
 *
 * Threads : every callback runs on the thread that ticks the scripts of the world, one script after the other, never two at once : what a
 * script does with shard_actors.h and shard_input.h is safe there. The functions of this table are "thread : main" (from on_load). */
#ifndef SHARD_SCRIPTS_H
#define SHARD_SCRIPTS_H

#include "shard_abi.h"
#include "shard_actors.h"
#include "shard_components.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_SCRIPTS "shard.scripts"

#define SHARD_SCRIPT_MAX_SIZE   16384u      /* bytes of an object */

/* The callbacks of a script. All are optional but construct and destruct. A callback returns SHARD_OK, or an error that the host logs and
 * counts as an error of the plugin (at the tenth the host fails the plugin, as for a system). `object` is what construct built. */
typedef ShardResult (SHARD_CALL *ShardScriptFn)(void* user, void* object);
typedef ShardResult (SHARD_CALL *ShardScriptTickFn)(void* user, void* object, float delta_time);

/* What a script is told about a contact of its actor's body with another one (physics, ABI 1.5). `other` is the other body's actor. `normal`
 * points from this actor toward the other one. `point` is a point of the contact in the world, `penetration` how deep the bodies overlap.
 * on_contact_ended only fills `other`. */
typedef struct ShardContact {
    uint32_t structSize;
    ShardActor other;
    float point[3];
    float normal[3];
    float penetration;
} ShardContact;

typedef ShardResult (SHARD_CALL *ShardScriptContactFn)(void* user, void* object, const ShardContact* contact);

typedef struct ShardScriptCallbacks {
    uint32_t structSize;

    /* Builds the object in `storage` : fields at their defaults, no actor yet. Called when the component is made (an actor getting it,
     * the world file read, a copy of an actor). SHARD_OK, or an error to refuse (the component is not made). */
    ShardResult (SHARD_CALL *construct)(void* user, void* storage);
    /* Destroys the object. The storage is the host's to free. */
    void        (SHARD_CALL *destruct)(void* user, void* storage);
    /* The component joined an actor : this is its owner, valid as long as the object lives. Called once, before on_create. */
    ShardResult (SHARD_CALL *attach)(void* user, void* object, ShardActor owner);

    ShardScriptFn     on_create;            /* the script joined an actor that is in a world (Script::OnCreate) */
    ShardScriptFn     on_world_loaded;      /* the world finished loading (for actors tagged "Persistent") */
    ShardScriptFn     on_world_unloaded;
    ShardScriptFn     on_play;              /* play mode starts */
    ShardScriptTickFn on_tick;              /* every frame, in the Update phase : delta_time is the frame's */
    ShardScriptTickFn on_fixed_tick;        /* every fixed step, right before the physics step : delta_time is the step's */
    ShardScriptTickFn on_late_tick;         /* every frame, after the scripts and the physics moved everything */
    ShardScriptFn     on_stop;              /* play mode stops */
    ShardScriptFn     on_destroyed;         /* the component or its actor is destroyed, before destruct */
    ShardScriptFn     on_activated;
    ShardScriptFn     on_deactivated;

    /* 1.5 : physics contacts, in the physics step's callbacks, on the thread that steps the physics */
    ShardScriptContactFn on_contact_added;
    ShardScriptContactFn on_contact_persisted;
    ShardScriptContactFn on_contact_ended;

    /* 1.5 : the plugin's own strings. A field SHARD_FIELD_STRING with SHARD_FIELD_ACCESSOR is the object's `std::string` (or any string of
     * the plugin's) at that offset : the host never reads its memory, it asks these. text_get gives the text (valid until the next call into
     * the object), text_set replaces it. Both are required when a field has the flag. */
    void        (SHARD_CALL *text_get)(void* user, void* object, uint32_t offset, ShardStr* out);
    ShardResult (SHARD_CALL *text_set)(void* user, void* object, uint32_t offset, ShardStr text);

    /* Callbacks of later ABI revisions go HERE : the host checks structSize before reading them. */
} ShardScriptCallbacks;

typedef struct ShardScriptDesc {
    uint32_t structSize;
    uint32_t size;                          /* bytes of the object, 1 to SHARD_SCRIPT_MAX_SIZE */
    uint32_t align;                         /* a power of two, 16 at most */
    uint32_t fieldCount;                    /* SHARD_COMPONENT_MAX_FIELDS at most */
    ShardStr name;                          /* "Health" : the host registers "<plugin id>.Health", the name a world file calls it */
    const ShardFieldDesc* fields;           /* offsets in the object. Read during the call only. */
    void* user;                             /* handed back to every callback */
    const ShardScriptCallbacks* callbacks;  /* copied up to its structSize during the call */
} ShardScriptDesc;

typedef struct ShardScriptsApi {
    uint32_t structSize;

    /* Registers a script type, from on_load. thread : main.
     * SHARD_E_INVALID_ARG : a field out of the object, misaligned or overlapping another, a bad name, a size or an alignment out of bounds,
     * no construct or destruct (the log says which). SHARD_E_ALREADY_EXISTS : the name is taken. */
    ShardResult (SHARD_CALL *register_script)(ShardPluginCtx ctx, const ShardScriptDesc* desc);

    /* The object of the script `name` (as registered, "Health") that `actor` has, if it belongs to this plugin : NULL in *out otherwise.
     * Valid until the script is removed or the actor destroyed. thread : script */
    ShardResult (SHARD_CALL *get_object)(ShardPluginCtx ctx, ShardActor actor, ShardStr name, void** out);

    /* Adds the script `name` to `actor` and returns its object. A script of this plugin only. thread : script */
    ShardResult (SHARD_CALL *add_to_actor)(ShardPluginCtx ctx, ShardActor actor, ShardStr name, void** out);
} ShardScriptsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_SCRIPTS_H */

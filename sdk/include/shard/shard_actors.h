/* Interface "shard.actors" 1.2 (1.0 + the fields of a component, one at a time in 1.1 ; stable ids, bounds and neighbours in 1.2) : the actors of a world, as a script sees them.
 *
 * An actor is what a world file holds and the editor's outliner shows : a name, a pose, children, tags and components. A ShardActor is a
 * handle to one (a generation and a slot, never a pointer) : it fails with SHARD_E_STALE_HANDLE once the actor is destroyed, and a handle
 * of another actor never takes its place. 0 is never valid.
 *
 * Where it may be called ("thread : script") : from the callbacks of a script (shard_scripts.h), from on_load / on_play_start... and the
 * other callbacks of the plugin that run on the main thread. NOT from a system (shard_systems.h) : a system runs beside others, and the
 * actors are not made for that (SHARD_E_WRONG_THREAD). A system moves an actor with shard.ecs' set_local_transform.
 *
 * "In the world of `anchor`" : the functions that create or look for actors work in the world the anchor actor is in. A script passes its
 * owner. The engine's components are reached by the name their serialized form carries ("light", "model"...), their fields as JSON text :
 * the format of the world files, so anything the editor shows can be read and set from here. */
#ifndef SHARD_ACTORS_H
#define SHARD_ACTORS_H

#include "shard_abi.h"
#include "shard_ecs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_ACTORS "shard.actors"

typedef uint64_t ShardActor;        /* 0 is never valid : "no actor" */

typedef struct ShardActorsApi {
    uint32_t structSize;

    /* ---- Identity and state ---- */
    /* SHARD_TRUE if the handle designates an actor that still exists. thread : script */
    int32_t     (SHARD_CALL *is_valid)(ShardActor actor);
    /* The name, as the "text out" of the ABI : at most `capacity` bytes into `buffer`, the full length in *size. thread : script */
    ShardResult (SHARD_CALL *get_name)(ShardActor actor, char* buffer, uint64_t capacity, uint64_t* size);
    ShardResult (SHARD_CALL *set_name)(ShardActor actor, ShardStr name);
    int32_t     (SHARD_CALL *is_active)(ShardActor actor);
    ShardResult (SHARD_CALL *set_active)(ShardActor actor, int32_t active);
    /* Removes the actor, its components (their scripts get on_destroyed) and its children. The handle is stale afterwards. */
    ShardResult (SHARD_CALL *destroy)(ShardActor actor);

    /* ---- Pose : ShardLocalTransform (shard_ecs.h), rotation as a quaternion x, y, z, w ---- */
    /* relative to the parent */
    ShardResult (SHARD_CALL *get_transform)(ShardActor actor, ShardLocalTransform* out);
    ShardResult (SHARD_CALL *set_transform)(ShardActor actor, const ShardLocalTransform* transform);
    /* in the world (the root reference frame's axes) ; the scale is the lossy one of a parent that is scaled and rotated */
    ShardResult (SHARD_CALL *get_world_transform)(ShardActor actor, ShardLocalTransform* out);
    ShardResult (SHARD_CALL *set_world_transform)(ShardActor actor, const ShardLocalTransform* transform);

    /* ---- Hierarchy ---- */
    /* *out is 0 for an actor at the root of the world */
    ShardResult (SHARD_CALL *get_parent)(ShardActor actor, ShardActor* out);
    /* parent 0 : the root of the world. keep_world SHARD_TRUE : the actor stays where it is (its local pose is recomputed). */
    ShardResult (SHARD_CALL *set_parent)(ShardActor actor, ShardActor parent, int32_t keep_world);
    ShardResult (SHARD_CALL *child_count)(ShardActor actor, uint32_t* count);
    ShardResult (SHARD_CALL *child_at)(ShardActor actor, uint32_t index, ShardActor* out);

    /* ---- Tags (docs/concepts/tags.md) : a tag is found by its name, "Enemy.Undead" ---- */
    ShardResult (SHARD_CALL *add_tag)(ShardActor actor, ShardStr tag);
    ShardResult (SHARD_CALL *remove_tag)(ShardActor actor, ShardStr tag);
    /* SHARD_TRUE if the actor has the tag or one under it ("Enemy" for an "Enemy.Undead") */
    int32_t     (SHARD_CALL *has_tag)(ShardActor actor, ShardStr tag);

    /* ---- The world of an actor ---- */
    /* A new actor (with its Transform) at the root of the world of `anchor`, or under `parent` (0 : the root). *out is the actor. */
    ShardResult (SHARD_CALL *create)(ShardActor anchor, ShardStr name, ShardActor parent, ShardActor* out);
    /* An actor built from the JSON of a world file's actor (name, components, children) : the form the editor saves prefabs in */
    ShardResult (SHARD_CALL *spawn)(ShardActor anchor, ShardStr actor_json, ShardActor parent, ShardActor* out);
    /* The first actor of that name, depth first in the order of the ids. SHARD_E_NOT_FOUND if there is none. */
    ShardResult (SHARD_CALL *find_by_name)(ShardActor anchor, ShardStr name, ShardActor* out);
    /* The actors that have the tag (or one under it), in the order of their ids : up to `capacity` of them into `out`, how many there are
     * in *count (call it with capacity 0 to ask). The deactivated ones are left out. */
    ShardResult (SHARD_CALL *find_by_tag)(ShardActor anchor, ShardStr tag, ShardActor* out, uint32_t capacity, uint32_t* count);

    /* ---- Components, by the name their serialized form carries ("light", "model", "physics_body", or "<plugin id>.<name>") ---- */
    /* Adds one. `fields_json` : the fields to set, a JSON object ("{ \"intensity\": 3 }"), or an empty string. SHARD_E_NOT_FOUND : unknown type. */
    ShardResult (SHARD_CALL *add_component)(ShardActor actor, ShardStr type, ShardStr fields_json);
    int32_t     (SHARD_CALL *has_component)(ShardActor actor, ShardStr type);
    ShardResult (SHARD_CALL *remove_component)(ShardActor actor, ShardStr type);
    /* The component as the world file writes it (a JSON object), as a "text out". SHARD_E_NOT_FOUND : the actor has none. */
    ShardResult (SHARD_CALL *get_component)(ShardActor actor, ShardStr type, char* buffer, uint64_t capacity, uint64_t* size);
    /* Sets the fields that the JSON object holds, like an edit in the inspector (the others keep their value) */
    ShardResult (SHARD_CALL *set_component)(ShardActor actor, ShardStr type, ShardStr fields_json);

    /* ---- One field of a component (1.1) ----
     * `path` is a field of the component ("intensity", "motionType", "active") or a path into it, the names and indexes of the world file's
     * JSON separated by '/' ("materials/0", "shapes/0/params/radius"). A value is JSON text : 2.5, true, "Dynamic", {"x":0,"y":1,"z":0}.
     * Assets read as their name in the project and are written as that name or as their id. SHARD_E_NOT_FOUND : no such component or field. */
    /* The value, as a "text out" */
    ShardResult (SHARD_CALL *get_component_field)(ShardActor actor, ShardStr type, ShardStr path, char* buffer, uint64_t capacity, uint64_t* size);
    /* Sets one field and nothing else : a field of the engine goes through the component's own change hook, like an edit in the inspector
     * (a physics body keeps its velocity when its friction changes). An array grows to hold the index it is given ("materials/2" on one
     * material: the gap holds 0) and "-" appends ("shapes/-"). SHARD_E_INVALID_ARG : the value does not fit the field. */
    ShardResult (SHARD_CALL *set_component_field)(ShardActor actor, ShardStr type, ShardStr path, ShardStr value_json);
    /* Removes an element of an array ("shapes/1") or a key of an object */
    ShardResult (SHARD_CALL *erase_component_field)(ShardActor actor, ShardStr type, ShardStr path);

    /* ---- 1.2 : identity across machines, and the neighbourhood ---- */
    /* A number that names the actor in a way two machines running the same game agree on : the world's asset id (high 32 bits) and the
     * actor's id in that world (low 32), the ids being given in the order actors are made, which the simulation reproduces. Use it to say
     * "this actor" in a network message, a save file or a replay. Not a handle : it never goes stale, it just stops finding anything. */
    ShardResult (SHARD_CALL *get_stable_id)(ShardActor actor, uint64_t* out);
    /* The actor that has that stable id, or SHARD_E_NOT_FOUND (it is not made yet, or is destroyed). */
    ShardResult (SHARD_CALL *find_by_stable_id)(uint64_t stable_id, ShardActor* out);
    /* The asset id of the world the actor is in (-1 for a world with no asset). */
    ShardResult (SHARD_CALL *get_world_id)(ShardActor actor, int32_t* out);
    /* The box that holds the actor's visible and physical shapes (its model, its physics body, its children's), in the world, axis aligned.
     * SHARD_E_NOT_FOUND : the actor has none of them -- use its position. */
    ShardResult (SHARD_CALL *get_world_bounds)(ShardActor actor, float out_min[3], float out_max[3]);
    /* The actors whose position is within `radius` of `center` (a sphere, in the world), or in the box [min, max]. `tag` restricts them (empty :
     * any). Same array out as find_by_tag : up to `capacity` into `out`, how many there are in *count ; the deactivated ones are left out.
     * Nearest first for the sphere, the order of the ids for the box. In the world of `anchor`. */
    ShardResult (SHARD_CALL *find_in_sphere)(ShardActor anchor, const float center[3], float radius, ShardStr tag, ShardActor* out, uint32_t capacity, uint32_t* count);
    ShardResult (SHARD_CALL *find_in_box)(ShardActor anchor, const float box_min[3], const float box_max[3], ShardStr tag, ShardActor* out, uint32_t capacity, uint32_t* count);
} ShardActorsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_ACTORS_H */

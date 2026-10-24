/* Interface "shard.physics" 1.1 : the rigid bodies of actors, ray casts and volume queries, and colliders a plugin builds itself.
 *
 * A body is the engine's "physics_body" component of an actor (add it with shard.actors' add_component, its shape and mass are fields of the
 * component). These functions move and push the body of an actor : its handle is the actor's. Positions, directions and velocities are in
 * the world, in the units of the project (one unit is one metre unless the project says otherwise), forces in newtons and impulses in
 * newton seconds. Contacts are not asked for here : a script is told about them (on_contact_* in shard_scripts.h).
 *
 * 1.1 added, from raycast_all on : queries that return every hit (rays, overlaps of a sphere, a box and a capsule, sphere and box sweeps) with
 * a filter (layers, an actor to ignore, a tag), and colliders the plugin builds from triangles or boxes (a voxel chunk, a navigation obstacle, a
 * heightfield cut into a mesh) that exist in the physics world without an actor. A hit on one of them reports its `collider` and `userData`.
 *
 * thread : script (shard_actors.h says where that is). SHARD_E_NOT_FOUND : the actor has no physics body, or it has no live body yet.
 * SHARD_E_UNSUPPORTED : this host has no physics (an offline tool). */
#ifndef SHARD_PHYSICS_H
#define SHARD_PHYSICS_H

#include "shard_abi.h"
#include "shard_actors.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_PHYSICS "shard.physics"

/* The layers a ray looks at (a bit mask) */
#define SHARD_LAYER_STATIC  1u      /* bodies that never move */
#define SHARD_LAYER_MOVING  2u      /* kinematic and dynamic bodies */
#define SHARD_LAYER_ALL     3u

typedef uint64_t ShardCollider;     /* a collider of the plugin (below) ; 0 is never valid */

typedef struct ShardRayHit {
    uint32_t structSize;
    ShardActor actor;               /* the actor whose body the ray hit (0 for a plugin collider that has no owner) */
    float point[3];                 /* where, in the world */
    float normal[3];                /* the surface's normal there, pointing out of the body */
    float distance;                 /* from the origin along the ray ; for an overlap, how deep the shapes overlap */
    /* ---- 1.1 : a function fills what fits in out->structSize, so a plugin built for 1.0 keeps working ---- */
    ShardCollider collider;         /* the plugin collider that was hit, 0 for the body of an actor */
    uint64_t userData;              /* that collider's userData */
    uint32_t triangle;              /* a mesh collider : the index of the triangle (its first index is 3 * triangle), otherwise 0 */
    uint32_t box;                   /* a box set : the index of the box, otherwise 0 */
} ShardRayHit;

#define SHARD_QUERY_COLLIDERS_ONLY  1u  /* the plugin colliders only, not the bodies of actors */
#define SHARD_QUERY_NO_COLLIDERS    2u  /* the bodies of actors only */

/* What a query looks at */
typedef struct ShardQueryFilter {
    uint32_t structSize;
    uint32_t layers;                /* SHARD_LAYER_* ; 0 : all */
    ShardActor ignore;              /* an actor whose body the query goes through, or 0 */
    uint32_t flags;                 /* SHARD_QUERY_* */
    ShardStr tag;                   /* only the actors that have this tag (or one under it). Empty : any. A plugin collider has no tag. */
} ShardQueryFilter;

/* A collider built from geometry. Static : it never moves by itself (collider_set_pose moves it), and bodies collide with it as with any other
 * static body. The geometry is copied during the call. */
#define SHARD_COLLIDER_MESH     1u  /* triangles : `vertices` (xyz) and `indices` (triangle list). Hollow : a body inside it falls out. */
#define SHARD_COLLIDER_BOXES    2u  /* a union of boxes aligned on the collider's axes : `boxes`, 6 floats each (centre xyz, half extent xyz) */

typedef struct ShardColliderDesc {
    uint32_t structSize;
    uint32_t kind;                  /* SHARD_COLLIDER_* */
    uint32_t vertexCount;           /* MESH */
    uint32_t indexCount;            /* MESH : a multiple of 3 */
    uint32_t boxCount;              /* BOXES */
    uint32_t reserved;
    const float* vertices;
    const uint32_t* indices;
    const float* boxes;
    ShardActor owner;               /* reported as the `actor` of a hit, 0 : none */
    uint64_t userData;              /* handed back in the hits : a chunk index, an id of your own */
    float friction;                 /* 0.5 is the usual */
    float restitution;
    double position[3];             /* the pose in the world */
    float  rotation[4];             /* quaternion x, y, z, w */
} ShardColliderDesc;

typedef struct ShardPhysicsApi {
    uint32_t structSize;

    /* Velocities, in the world */
    ShardResult (SHARD_CALL *get_velocity)(ShardActor actor, float out[3]);
    ShardResult (SHARD_CALL *set_velocity)(ShardActor actor, const float velocity[3]);
    ShardResult (SHARD_CALL *get_angular_velocity)(ShardActor actor, float out[3]);
    ShardResult (SHARD_CALL *set_angular_velocity)(ShardActor actor, const float velocity[3]);

    /* A force acts for the step it is added in (call it every step to push for longer), an impulse changes the velocity at once. A
     * point is in the world : a force or impulse off the centre of mass also turns the body. */
    ShardResult (SHARD_CALL *add_force)(ShardActor actor, const float force[3]);
    ShardResult (SHARD_CALL *add_force_at_point)(ShardActor actor, const float force[3], const float point[3]);
    ShardResult (SHARD_CALL *add_torque)(ShardActor actor, const float torque[3]);
    ShardResult (SHARD_CALL *add_impulse)(ShardActor actor, const float impulse[3]);
    ShardResult (SHARD_CALL *add_impulse_at_point)(ShardActor actor, const float impulse[3], const float point[3]);
    ShardResult (SHARD_CALL *add_angular_impulse)(ShardActor actor, const float impulse[3]);

    /* The acceleration the body falls with now, from the world's gravity and the gravity fields where it is (or its own custom gravity) */
    ShardResult (SHARD_CALL *get_gravity)(ShardActor actor, float out[3]);
    /* Gives the body a gravity of its own : the world's gravity and the fields no longer reach it */
    ShardResult (SHARD_CALL *set_custom_gravity)(ShardActor actor, const float gravity[3]);

    /* The first body a ray meets. `direction` need not be normalized (a zero one is SHARD_E_INVALID_ARG). `layers` : SHARD_LAYER_*. `ignore` :
     * an actor whose body the ray goes through (the one that fires it), or 0. SHARD_OK and *out filled when something is hit,
     * SHARD_E_NOT_FOUND when nothing is within `max_distance`. */
    ShardResult (SHARD_CALL *raycast)(const float origin[3], const float direction[3], float max_distance, uint32_t layers, ShardActor ignore, ShardRayHit* out);

    /* ---- 1.1 : queries ----
     * `filter` may be NULL (everything). The ones that return several hits write up to `capacity` of them into `out` and the number found into
     * *count (which may be more than `capacity`), SHARD_OK even when there is none ; ask with capacity 0. Each hit's structSize must be set. */
    /* Every body along the ray, nearest first. */
    ShardResult (SHARD_CALL *raycast_all)(const float origin[3], const float direction[3], float max_distance, const ShardQueryFilter* filter,
                                          ShardRayHit* out, uint32_t capacity, uint32_t* count);
    /* The bodies that touch the shape. point : a point of the contact, normal : the direction to push the shape out, distance : by how much.
     * In no particular order, but the same one for the same world state. */
    ShardResult (SHARD_CALL *overlap_sphere)(const float center[3], float radius, const ShardQueryFilter* filter,
                                             ShardRayHit* out, uint32_t capacity, uint32_t* count);
    /* `rotation` is a quaternion x, y, z, w (NULL : aligned on the axes) */
    ShardResult (SHARD_CALL *overlap_box)(const float center[3], const float half_extent[3], const float rotation[4], const ShardQueryFilter* filter,
                                          ShardRayHit* out, uint32_t capacity, uint32_t* count);
    /* A capsule between the centres of its two end spheres */
    ShardResult (SHARD_CALL *overlap_capsule)(const float from[3], const float to[3], float radius, const ShardQueryFilter* filter,
                                              ShardRayHit* out, uint32_t capacity, uint32_t* count);
    /* Moves the shape along `direction` (need not be unit) up to max_distance and reports the first body it meets : distance is how far it got,
     * point the contact. SHARD_E_NOT_FOUND : nothing in the way. The shape must not start inside what it is tested against. */
    ShardResult (SHARD_CALL *sweep_sphere)(const float origin[3], float radius, const float direction[3], float max_distance,
                                           const ShardQueryFilter* filter, ShardRayHit* out);
    ShardResult (SHARD_CALL *sweep_box)(const float origin[3], const float half_extent[3], const float rotation[4], const float direction[3],
                                        float max_distance, const ShardQueryFilter* filter, ShardRayHit* out);

    /* ---- 1.1 : colliders of the plugin ----
     * They are released with the plugin. The body appears in the physics world at the next fixed step (a query before that does not see it). */
    /* Building a mesh shape can take a while for a big chunk : call it from a job (thread : any). SHARD_E_INVALID_ARG : empty or out of range
     * indices, a value that is not finite. */
    ShardResult (SHARD_CALL *collider_create)(ShardPluginCtx ctx, const ShardColliderDesc* desc, ShardCollider* out);
    /* Moves it. thread : script */
    ShardResult (SHARD_CALL *collider_set_pose)(ShardPluginCtx ctx, ShardCollider collider, const double position[3], const float rotation[4]);
    /* Replaces its geometry, keeping the handle and the pose (a voxel chunk was edited) : the same fields as create. thread : any */
    ShardResult (SHARD_CALL *collider_update)(ShardPluginCtx ctx, ShardCollider collider, const ShardColliderDesc* desc);
    ShardResult (SHARD_CALL *collider_destroy)(ShardPluginCtx ctx, ShardCollider collider);
} ShardPhysicsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_PHYSICS_H */

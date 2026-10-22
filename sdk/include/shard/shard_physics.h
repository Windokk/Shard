/* Interface "shard.physics" 1.0 : the rigid bodies of actors, and ray casts.
 *
 * A body is the engine's "physics_body" component of an actor (add it with shard.actors' add_component, its shape and mass are fields of the
 * component). These functions move and push the body of an actor : its handle is the actor's. Positions, directions and velocities are in
 * the world, in the units of the project (one unit is one metre unless the project says otherwise), forces in newtons and impulses in
 * newton seconds. Contacts are not asked for here : a script is told about them (on_contact_* in shard_scripts.h).
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

typedef struct ShardRayHit {
    uint32_t structSize;
    ShardActor actor;               /* the actor whose body the ray hit */
    float point[3];                 /* where, in the world */
    float normal[3];                /* the surface's normal there, pointing out of the body */
    float distance;                 /* from the origin along the ray */
} ShardRayHit;

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
} ShardPhysicsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_PHYSICS_H */

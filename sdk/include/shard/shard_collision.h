/* Interface "shard.collision" 1.0 : collision profiles, overlap and hit events, temporary changes and the reason two actors do or do not collide.
 *
 * A collider has a profile (a project asset, or one a plugin declares in its manifest) that says what it is and how it reacts to the others.
 * This interface reads and changes that at run time. The profiles themselves are data : "collision_profiles" in the project file or the plugin
 * manifest, "collision": true on the tags that are part of a collider's identity.
 *
 * Events. subscribe asks for the events of one actor (the ones of all its colliders). Nothing is produced for a pair until something listens
 * for it. The plugin reads them with poll_events, after the physics step that made them : they come in the same order for the same simulation.
 * A script gets the same events through on_overlap_begin, on_overlap_end and on_hit (shard_scripts.h) with no subscription to make.
 * Every overlap Begin is followed by an End, with a cause when the collider went away or changed.
 *
 * Queries follow the rules of a profile or an actor through the `as_profile` and `as_actor` fields of shard.physics' ShardQueryFilter.
 *
 * thread : script. Subscriptions, overrides and everything made through the context go with the plugin. SHARD_E_NOT_FOUND : the actor has no
 * live physics body. SHARD_E_UNSUPPORTED : this host has no physics. */
#ifndef SHARD_COLLISION_H
#define SHARD_COLLISION_H

#include "shard_abi.h"
#include "shard_actors.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_COLLISION "shard.collision"

typedef uint64_t ShardCollisionSub;     /* 0 is never a subscription */

/* The kinds of event, as a bit mask for subscribe and as a value in ShardCollisionEvent::kind */
#define SHARD_COLLISION_OVERLAP_BEGIN   1u  /* two colliders that overlap started touching */
#define SHARD_COLLISION_OVERLAP_END     2u  /* they stopped, or one went away (see cause) */
#define SHARD_COLLISION_HIT             4u  /* two colliders that block each other came into contact */
#define SHARD_COLLISION_ALL             7u

/* Why an overlap ended */
#define SHARD_COLLISION_CAUSE_NONE              0u
#define SHARD_COLLISION_CAUSE_SEPARATED         1u  /* the shapes stopped touching */
#define SHARD_COLLISION_CAUSE_DESTROYED         2u  /* an actor or collider was destroyed */
#define SHARD_COLLISION_CAUSE_DISABLED          3u  /* the body or its mode was disabled */
#define SHARD_COLLISION_CAUSE_PROFILE_CHANGED   4u  /* the pair is now ignored */
#define SHARD_COLLISION_CAUSE_TELEPORTED        5u  /* the position was set directly */

typedef struct ShardCollisionEvent {
    uint32_t structSize;
    uint32_t kind;                  /* one SHARD_COLLISION_* kind */
    uint64_t step;                  /* the fixed step it happened in */
    ShardActor self;                /* the actor that listens */
    ShardActor other;               /* what it touched */
    double point[3];                /* where, in the world (64 bits : far from the origin it stays exact) */
    float normal[3];                /* the direction pointing out of `other` */
    float impactSpeed;              /* speed of approach along the normal, before the contact is resolved (hits) */
    float impulse;                  /* estimated from the impact speed and the masses (hits) */
    uint32_t cause;                 /* SHARD_COLLISION_CAUSE_*, for an overlap end */
    uint32_t reserved;
    uint8_t surface[64];            /* the surface tag of `other`'s material as NUL-terminated text, or empty */
} ShardCollisionEvent;

#define SHARD_COLLISION_ONCE    1u  /* subscribe's `flags` : the subscription ends after its first event */

typedef struct ShardCollisionApi {
    uint32_t structSize;

    /* Starts listening to the events of `actor` (all its colliders). `kinds` : SHARD_COLLISION_*. `other` : a tag query on the other collider
     * ("Enemy & !Status.Dead"), empty for anything. `min_impact_speed` : hits slower than this are not reported. `flags` : SHARD_COLLISION_ONCE. */
    ShardResult (SHARD_CALL *subscribe)(ShardPluginCtx ctx, ShardActor actor, uint32_t kinds, ShardStr other, float min_impact_speed, uint32_t flags, ShardCollisionSub* out);
    ShardResult (SHARD_CALL *unsubscribe)(ShardPluginCtx ctx, ShardCollisionSub sub);
    /* The events the subscription received since the last poll, oldest first : up to `capacity` of them into `out` (each structSize set) and
     * the number written into *count. What does not fit stays for the next poll. At most 4096 wait : past that the oldest are dropped. */
    ShardResult (SHARD_CALL *poll_events)(ShardPluginCtx ctx, ShardCollisionSub sub, ShardCollisionEvent* out, uint32_t capacity, uint32_t* count);

    /* The actors `actor` overlaps right now. The number found goes into *count (which may be more than `capacity`) ; ask with capacity 0. */
    ShardResult (SHARD_CALL *get_overlaps)(ShardPluginCtx ctx, ShardActor actor, ShardActor* out, uint32_t capacity, uint32_t* count);

    /* The reason for the result between two actors, as text : the mode and motion of each, the rule that matched on each side with its profile
     * and position, any override and who set it, the final response. The text out of the ABI : `capacity` bytes into `buffer`, the full length
     * in *size. */
    ShardResult (SHARD_CALL *explain)(ShardPluginCtx ctx, ShardActor a, ShardActor b, char* buffer, uint64_t capacity, uint64_t* size);

    /* Gives the collider a profile by name (empty : back to the default of its motion type). SHARD_E_NOT_FOUND : no such profile. */
    ShardResult (SHARD_CALL *set_profile)(ShardPluginCtx ctx, ShardActor actor, ShardStr profile);

    /* A temporary change of a collider. It folds over the profile by priority (higher wins) until it is popped, and goes with the plugin.
     * `name` : who asks, shown in explain ("dash"). `ignore` : a tag query, the colliders it matches are ignored while the override lives
     * (empty : none). `mode` : 0 unchanged, 1 Solid, 2 Sensor, 3 QueryOnly, 4 Disabled. `default_response` : 0 unchanged, 1 Ignore, 2 Overlap, 3 Block. */
    ShardResult (SHARD_CALL *push_override)(ShardPluginCtx ctx, ShardActor actor, ShardStr name, int32_t priority, ShardStr ignore, uint32_t mode, uint32_t default_response, uint64_t* out);
    ShardResult (SHARD_CALL *pop_override)(ShardPluginCtx ctx, ShardActor actor, uint64_t override_id);

    /* Skips one pair of actors for good (a projectile and the actor that fired it) until ignore is 0 or one of them is destroyed. */
    ShardResult (SHARD_CALL *ignore_collision)(ShardPluginCtx ctx, ShardActor a, ShardActor b, int32_t ignore);
} ShardCollisionApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_COLLISION_H */

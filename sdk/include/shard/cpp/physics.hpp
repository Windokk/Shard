// Shard C++ SDK : ray casts (shard_physics.h). The forces, impulses and velocities of a body are methods of Shard::Actor.
//
//   if (auto hit = Shard::Physics::Raycast(eye, forward, 50.f, Shard::Layers::All, Owner())) {
//       hit->actor.Destroy();
//       Spawn(hit->point + hit->normal * 0.1f);
//   }
#pragma once

#include <optional>

#include <shard/shard_physics.h>
#include <shard/cpp/actor.hpp>

namespace Shard {

    /// Which bodies a ray looks at
    struct Layers {
        static constexpr uint32_t Static = SHARD_LAYER_STATIC;      ///< bodies that never move
        static constexpr uint32_t Moving = SHARD_LAYER_MOVING;      ///< kinematic and dynamic bodies
        static constexpr uint32_t All = SHARD_LAYER_ALL;
    };

    struct RayHit {
        Actor actor;        ///< the actor whose body was hit
        Vec3 point;         ///< where, in the world
        Vec3 normal;        ///< the surface's normal there, pointing out of the body
        float distance = 0.f;
    };

    class Physics {
    public:
        /// The first body the ray meets within `maxDistance`. `direction` need not be normalized. `ignore` : an actor whose body the ray goes
        /// through, usually the one that fires it. Call it from the callbacks of a script.
        static std::optional<RayHit> Raycast(Vec3 origin, Vec3 direction, float maxDistance, uint32_t layers = Layers::All, Actor ignore = Actor())
        {
            float o[3], d[3];
            detail::ToArray(origin, o);
            detail::ToArray(direction, d);
            ShardRayHit hit{};
            hit.structSize = sizeof(ShardRayHit);
            if (detail::PhysicsApi().raycast(o, d, maxDistance, layers, ignore.Handle(), &hit) != SHARD_OK) return std::nullopt;
            return RayHit{ Actor(hit.actor), detail::FromArray(hit.point), detail::FromArray(hit.normal), hit.distance };
        }
    };
}

// Shard C++ SDK : ray casts (shard_physics.h). The forces, impulses and velocities of a body are methods of Shard::Actor.
//
//   if (auto hit = Shard::Physics::Raycast(eye, forward, 50.f, Shard::Layers::All, Owner())) {
//       hit->actor.Destroy();
//       Spawn(hit->point + hit->normal * 0.1f);
//   }
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
        Actor actor;        ///< the actor whose body was hit (invalid for a collider with no owner)
        Vec3 point;         ///< where, in the world
        Vec3 normal;        ///< the surface normal there, pointing out of the body ; for an overlap, the way to push the shape out
        float distance = 0.f;       ///< from the origin along the ray ; for an overlap, how deep the shapes overlap
        ShardCollider collider = 0; ///< a collider of the plugin that was hit (ABI 1.7), 0 for the body of an actor
        uint64_t userData = 0;      ///< that collider's userData
        uint32_t triangle = 0;      ///< a mesh collider : the index of the triangle
        uint32_t box = 0;           ///< a box set : the index of the box
    };

    /// What a query looks at (ABI 1.7)
    struct QueryFilter {
        uint32_t layers = Layers::All;
        Actor ignore;               ///< an actor whose body the query goes through
        bool collidersOnly = false; ///< only the colliders of plugins
        bool noColliders = false;   ///< only the bodies of actors
        std::string tag;            ///< only the actors that have this tag (or one under it)
    };

    /// A static collider the plugin builds (a voxel chunk, a navigation obstacle, a heightfield cut into a mesh) : it exists in the physics world with no actor
    /// and goes with the object. It appears at the next fixed step. (ABI 1.7)
    class Collider {
    public:
        Collider() = default;
        Collider(Collider&& o) noexcept : m_Handle(o.m_Handle) { o.m_Handle = 0; }
        Collider& operator=(Collider&& o) noexcept { if (this != &o) { Reset(); m_Handle = o.m_Handle; o.m_Handle = 0; } return *this; }
        Collider(const Collider&) = delete;
        Collider& operator=(const Collider&) = delete;
        ~Collider() { Reset(); }

        /// Triangles : `vertices` 3 floats each, `indices` a triangle list. thread : any (a mesh shape can take a while : build it in a job).
        static Collider Mesh(const float* vertices, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount, Vec3 position = {}, Quat rotation = {}, uint64_t userData = 0, Actor owner = Actor())
        {
            ShardColliderDesc d = Base(SHARD_COLLIDER_MESH, position, rotation, userData, owner);
            d.vertexCount = vertexCount; d.indexCount = indexCount; d.vertices = vertices; d.indices = indices;
            return Make(d);
        }
        /// A union of boxes aligned on the collider axes : 6 floats each (centre xyz, half extent xyz)
        static Collider Boxes(const float* boxes, uint32_t boxCount, Vec3 position = {}, Quat rotation = {}, uint64_t userData = 0, Actor owner = Actor())
        {
            ShardColliderDesc d = Base(SHARD_COLLIDER_BOXES, position, rotation, userData, owner);
            d.boxCount = boxCount; d.boxes = boxes;
            return Make(d);
        }
        explicit operator bool() const { return m_Handle != 0; }
        ShardCollider Handle() const { return m_Handle; }
        bool SetPose(Vec3 position, Quat rotation = {}) const
        {
            const double p[3] = { position.x, position.y, position.z };
            const float q[4] = { rotation.x, rotation.y, rotation.z, rotation.w };
            return detail::PhysicsApi().collider_set_pose(detail::PluginHandle(), m_Handle, p, q) == SHARD_OK;
        }
        /// Replaces the geometry (a chunk was edited) with a new mesh
        bool UpdateMesh(const float* vertices, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount) const
        {
            ShardColliderDesc d = Base(SHARD_COLLIDER_MESH, {}, {}, 0, Actor());
            d.vertexCount = vertexCount; d.indexCount = indexCount; d.vertices = vertices; d.indices = indices;
            return detail::PhysicsApi().collider_update(detail::PluginHandle(), m_Handle, &d) == SHARD_OK;
        }
        void Reset()
        {
            if (m_Handle && detail::Host()) {
                if (const ShardPhysicsApi* api = detail::Interface<ShardPhysicsApi>(SHARD_IFACE_PHYSICS, 1)) api->collider_destroy(detail::PluginHandle(), m_Handle);
            }
            m_Handle = 0;
        }

    private:
        static ShardColliderDesc Base(uint32_t kind, Vec3 position, Quat rotation, uint64_t userData, Actor owner)
        {
            ShardColliderDesc d{};
            d.structSize = sizeof(ShardColliderDesc);
            d.kind = kind; d.owner = owner.Handle(); d.userData = userData; d.friction = 0.5f;
            d.position[0] = position.x; d.position[1] = position.y; d.position[2] = position.z;
            d.rotation[0] = rotation.x; d.rotation[1] = rotation.y; d.rotation[2] = rotation.z; d.rotation[3] = rotation.w;
            return d;
        }
        static Collider Make(const ShardColliderDesc& d)
        {
            Collider c;
            detail::Check(detail::PhysicsApi().collider_create(detail::PluginHandle(), &d, &c.m_Handle), "a collider");
            return c;
        }
        ShardCollider m_Handle = 0;
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
            return Convert(hit);
        }

        // ---- Queries with a filter (ABI 1.7) ----
        /// Every body along the ray, nearest first
        static std::vector<RayHit> RaycastAll(Vec3 origin, Vec3 direction, float maxDistance, const QueryFilter& filter = {})
        {
            float o[3], d[3];
            detail::ToArray(origin, o);
            detail::ToArray(direction, d);
            const Filter f(filter);
            return Collect([&](ShardRayHit* out, uint32_t capacity, uint32_t* count) { return detail::PhysicsApi().raycast_all(o, d, maxDistance, &f.raw, out, capacity, count); });
        }
        /// The bodies that touch the shape
        static std::vector<RayHit> OverlapSphere(Vec3 center, float radius, const QueryFilter& filter = {})
        {
            float c[3];
            detail::ToArray(center, c);
            const Filter f(filter);
            return Collect([&](ShardRayHit* out, uint32_t capacity, uint32_t* count) { return detail::PhysicsApi().overlap_sphere(c, radius, &f.raw, out, capacity, count); });
        }
        static std::vector<RayHit> OverlapBox(Vec3 center, Vec3 halfExtent, Quat rotation = {}, const QueryFilter& filter = {})
        {
            float c[3], h[3], q[4] = { rotation.x, rotation.y, rotation.z, rotation.w };
            detail::ToArray(center, c);
            detail::ToArray(halfExtent, h);
            const Filter f(filter);
            return Collect([&](ShardRayHit* out, uint32_t capacity, uint32_t* count) { return detail::PhysicsApi().overlap_box(c, h, q, &f.raw, out, capacity, count); });
        }
        static std::vector<RayHit> OverlapCapsule(Vec3 from, Vec3 to, float radius, const QueryFilter& filter = {})
        {
            float a[3], b[3];
            detail::ToArray(from, a);
            detail::ToArray(to, b);
            const Filter f(filter);
            return Collect([&](ShardRayHit* out, uint32_t capacity, uint32_t* count) { return detail::PhysicsApi().overlap_capsule(a, b, radius, &f.raw, out, capacity, count); });
        }
        /// The first body a sphere meets going along `direction` : `distance` is how far it got
        static std::optional<RayHit> SweepSphere(Vec3 origin, float radius, Vec3 direction, float maxDistance, const QueryFilter& filter = {})
        {
            float o[3], d[3];
            detail::ToArray(origin, o);
            detail::ToArray(direction, d);
            const Filter f(filter);
            ShardRayHit hit{};
            hit.structSize = sizeof(ShardRayHit);
            if (detail::PhysicsApi().sweep_sphere(o, radius, d, maxDistance, &f.raw, &hit) != SHARD_OK) return std::nullopt;
            return Convert(hit);
        }
        static std::optional<RayHit> SweepBox(Vec3 origin, Vec3 halfExtent, Quat rotation, Vec3 direction, float maxDistance, const QueryFilter& filter = {})
        {
            float o[3], h[3], d[3], q[4] = { rotation.x, rotation.y, rotation.z, rotation.w };
            detail::ToArray(origin, o);
            detail::ToArray(halfExtent, h);
            detail::ToArray(direction, d);
            const Filter f(filter);
            ShardRayHit hit{};
            hit.structSize = sizeof(ShardRayHit);
            if (detail::PhysicsApi().sweep_box(o, h, q, d, maxDistance, &f.raw, &hit) != SHARD_OK) return std::nullopt;
            return Convert(hit);
        }

    private:
        // The ABI filter over a QueryFilter (the tag text must outlive the call)
        struct Filter {
            explicit Filter(const QueryFilter& q) : tag(q.tag)
            {
                raw.structSize = sizeof(ShardQueryFilter);
                raw.layers = q.layers;
                raw.ignore = q.ignore.Handle();
                raw.flags = (q.collidersOnly ? SHARD_QUERY_COLLIDERS_ONLY : 0u) | (q.noColliders ? SHARD_QUERY_NO_COLLIDERS : 0u);
                raw.tag = Str(tag);
            }
            std::string tag;
            ShardQueryFilter raw{};
        };
        static RayHit Convert(const ShardRayHit& hit)
        {
            RayHit out{ Actor(hit.actor), detail::FromArray(hit.point), detail::FromArray(hit.normal), hit.distance };
            out.collider = hit.collider; out.userData = hit.userData; out.triangle = hit.triangle; out.box = hit.box;
            return out;
        }
        template <typename Call>
        static std::vector<RayHit> Collect(Call&& call)
        {
            std::vector<RayHit> out;
            uint32_t count = 0;
            if (call(nullptr, 0u, &count) != SHARD_OK || count == 0) return out;
            std::vector<ShardRayHit> raw(count);
            for (ShardRayHit& r : raw) r.structSize = sizeof(ShardRayHit);
            if (call(raw.data(), count, &count) != SHARD_OK) return out;
            for (uint32_t i = 0; i < count && i < raw.size(); ++i) out.push_back(Convert(raw[i]));
            return out;
        }
    };
}

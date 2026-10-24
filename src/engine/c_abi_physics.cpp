#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include <glm/glm.hpp>

#include <shard/shard_physics.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CompoundShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>

#include "engine/core/tags/tag.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_events.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/plugins/abi/c_handles.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"

// shard.physics : the PhysicsBody of an actor, the physics manager's queries, and the colliders a plugin builds itself. Lives in the engine root,
// not in src/engine/plugins/abi with the other interfaces: the plugin layer is below physics and may not include it (scripts/check_deps.py), and
// c_internal.hpp declares what it needs from here (PhysicsApi, DecodeContact).

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        using Objects::Components::PhysicsBody;

        glm::vec3 Vec(const float v[3]) { return glm::vec3(v[0], v[1], v[2]); }
        void Out(const glm::vec3& v, float out[3]) { out[0] = v.x; out[1] = v.y; out[2] = v.z; }
        bool Finite(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
        bool FiniteD(const double v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
        bool FiniteQ(const float q[4]) { return std::isfinite(q[0]) && std::isfinite(q[1]) && std::isfinite(q[2]) && std::isfinite(q[3]) && (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]) > 1e-12f; }

        Physics::PhysicsManager* Manager()
        {
            Core::IEngineContext* engine = Core::gSharedEnginePtr;
            return engine ? engine->GetPhysicsManager() : nullptr;
        }

        // The live body of an actor's physics component
        ShardResult BodyOf(ShardActor handle, std::shared_ptr<PhysicsBody>& out)
        {
            if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
            std::shared_ptr<Objects::Actor> actor = ActorOf(handle);
            if (!actor) return SHARD_E_STALE_HANDLE;
            Core::IEngineContext* engine = Core::gSharedEnginePtr;
            if (!engine) return SHARD_E_UNSUPPORTED;
            for (const auto& body : actor->GetComponents<PhysicsBody>()) {
                if (body->GetBodyID().IsInvalid()) continue;
                out = body;
                return SHARD_OK;
            }
            return SHARD_E_NOT_FOUND;
        }

        template <typename Fn>
        ShardResult WithBody(ShardActor actor, const float* vector, Fn&& fn)
        {
            try {
                if (vector && !Finite(vector)) return SHARD_E_INVALID_ARG;
                std::shared_ptr<PhysicsBody> body;
                const ShardResult r = BodyOf(actor, body);
                if (r != SHARD_OK) return r;
                fn(*body);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL GetVelocity(ShardActor actor, float out[3])
        {
            if (!out) return SHARD_E_INVALID_ARG;
            return WithBody(actor, nullptr, [&](PhysicsBody& b) { Out(b.GetLinearVelocity(), out); });
        }
        ShardResult SHARD_CALL SetVelocity(ShardActor actor, const float v[3])
        {
            if (!v) return SHARD_E_INVALID_ARG;
            return WithBody(actor, v, [&](PhysicsBody& b) { b.SetLinearVelocity(Vec(v)); });
        }
        ShardResult SHARD_CALL GetAngularVelocity(ShardActor actor, float out[3])
        {
            if (!out) return SHARD_E_INVALID_ARG;
            return WithBody(actor, nullptr, [&](PhysicsBody& b) { Out(b.GetAngularVelocity(), out); });
        }
        ShardResult SHARD_CALL SetAngularVelocity(ShardActor actor, const float v[3])
        {
            if (!v) return SHARD_E_INVALID_ARG;
            return WithBody(actor, v, [&](PhysicsBody& b) { b.SetAngularVelocity(Vec(v)); });
        }
        ShardResult SHARD_CALL AddForce(ShardActor actor, const float f[3])
        {
            if (!f) return SHARD_E_INVALID_ARG;
            return WithBody(actor, f, [&](PhysicsBody& b) { b.AddForce(Vec(f)); });
        }
        ShardResult SHARD_CALL AddForceAtPoint(ShardActor actor, const float f[3], const float p[3])
        {
            if (!f || !p || !Finite(p)) return SHARD_E_INVALID_ARG;
            return WithBody(actor, f, [&](PhysicsBody& b) { b.AddForceAtPoint(Vec(f), Vec(p)); });
        }
        ShardResult SHARD_CALL AddTorque(ShardActor actor, const float t[3])
        {
            if (!t) return SHARD_E_INVALID_ARG;
            return WithBody(actor, t, [&](PhysicsBody& b) { b.AddTorque(Vec(t)); });
        }
        ShardResult SHARD_CALL AddImpulse(ShardActor actor, const float i[3])
        {
            if (!i) return SHARD_E_INVALID_ARG;
            return WithBody(actor, i, [&](PhysicsBody& b) { b.AddImpulse(Vec(i)); });
        }
        ShardResult SHARD_CALL AddImpulseAtPoint(ShardActor actor, const float i[3], const float p[3])
        {
            if (!i || !p || !Finite(p)) return SHARD_E_INVALID_ARG;
            return WithBody(actor, i, [&](PhysicsBody& b) { b.AddImpulseAtPoint(Vec(i), Vec(p)); });
        }
        ShardResult SHARD_CALL AddAngularImpulse(ShardActor actor, const float i[3])
        {
            if (!i) return SHARD_E_INVALID_ARG;
            return WithBody(actor, i, [&](PhysicsBody& b) { b.AddAngularImpulse(Vec(i)); });
        }
        ShardResult SHARD_CALL GetGravity(ShardActor actor, float out[3])
        {
            if (!out) return SHARD_E_INVALID_ARG;
            return WithBody(actor, nullptr, [&](PhysicsBody& b) { Out(b.GetGravity(), out); });
        }
        ShardResult SHARD_CALL SetCustomGravity(ShardActor actor, const float g[3])
        {
            if (!g) return SHARD_E_INVALID_ARG;
            return WithBody(actor, g, [&](PhysicsBody& b) { b.SetCustomGravity(Vec(g)); });
        }

        // ---- The colliders a plugin builds ----

        struct Collider {
            PluginOwner plugin = 0;
            uint64_t handle = 0;
            uint32_t kind = 0;
            JPH::RefConst<JPH::Shape> shape;        // the shape not yet given to the body (a new one, or an update)
            JPH::BodyID body;                       // invalid until the next frame begins
            ShardActor owner = 0;
            uint64_t userData = 0;
            float friction = 0.5f, restitution = 0.0f;
            JPH::RVec3 position = JPH::RVec3::sZero();
            JPH::Quat rotation = JPH::Quat::sIdentity();
            bool poseDirty = false;
            bool shapeDirty = false;
            bool destroyed = false;
        };

        Plugins::CAbi::OwnedTable<Collider> gColliders;
        std::mutex gColliderMutex;                  // the fields above, between the plugin's threads and the frame
        std::vector<std::shared_ptr<Collider>> gToApply;    // created, updated or destroyed since the frame began
        std::mutex gReleaseMutex;
        std::unordered_map<PluginOwner, bool> gReleaseHooked;

        // A mesh or a box set as a Jolt shape, built on the calling thread (it can take a while)
        JPH::RefConst<JPH::Shape> BuildShape(const ShardColliderDesc& desc, std::string& why)
        {
            if (desc.kind == SHARD_COLLIDER_MESH) {
                if (!desc.vertices || !desc.indices || desc.vertexCount == 0 || desc.indexCount == 0 || desc.indexCount % 3 != 0) { why = "a mesh collider needs vertices and a multiple of 3 indices"; return nullptr; }
                for (uint32_t i = 0; i < desc.indexCount; ++i)
                    if (desc.indices[i] >= desc.vertexCount) { why = "an index of the mesh collider is out of range"; return nullptr; }
                for (uint32_t i = 0; i < desc.vertexCount * 3; ++i)
                    if (!std::isfinite(desc.vertices[i])) { why = "a vertex of the mesh collider is not finite"; return nullptr; }
                JPH::VertexList vertices;
                vertices.reserve(desc.vertexCount);
                for (uint32_t i = 0; i < desc.vertexCount; ++i) vertices.push_back(JPH::Float3(desc.vertices[i * 3], desc.vertices[i * 3 + 1], desc.vertices[i * 3 + 2]));
                JPH::IndexedTriangleList triangles;
                triangles.reserve(desc.indexCount / 3);
                for (uint32_t t = 0; t < desc.indexCount / 3; ++t)
                    triangles.push_back(JPH::IndexedTriangle(desc.indices[t * 3], desc.indices[t * 3 + 1], desc.indices[t * 3 + 2], 0, t));
                JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
                settings.mPerTriangleUserData = true;       // the index of a triangle comes back in the hits
                const JPH::ShapeSettings::ShapeResult result = settings.Create();
                if (result.HasError()) { why = std::string("Jolt refused the mesh : ") + result.GetError().c_str(); return nullptr; }
                return result.Get();
            }
            if (desc.kind == SHARD_COLLIDER_BOXES) {
                if (!desc.boxes || desc.boxCount == 0) { why = "a box collider needs boxes"; return nullptr; }
                JPH::StaticCompoundShapeSettings settings;
                for (uint32_t i = 0; i < desc.boxCount; ++i) {
                    const float* b = desc.boxes + i * 6;
                    for (int k = 0; k < 6; ++k) if (!std::isfinite(b[k])) { why = "a box of the collider is not finite"; return nullptr; }
                    const JPH::Vec3 half(std::max(std::abs(b[3]), 0.001f), std::max(std::abs(b[4]), 0.001f), std::max(std::abs(b[5]), 0.001f));
                    const float radius = std::min(0.05f, std::min(half.GetX(), std::min(half.GetY(), half.GetZ())));
                    settings.AddShape(JPH::Vec3(b[0], b[1], b[2]), JPH::Quat::sIdentity(), new JPH::BoxShape(half, radius));
                }
                const JPH::ShapeSettings::ShapeResult result = settings.Create();
                if (result.HasError()) { why = std::string("Jolt refused the boxes : ") + result.GetError().c_str(); return nullptr; }
                return result.Get();
            }
            why = "unknown collider kind " + std::to_string(desc.kind);
            return nullptr;
        }

        ShardResult ColliderDescriptor(PluginContext& ctx, const ShardColliderDesc* desc, JPH::RefConst<JPH::Shape>& shape)
        {
            if (!desc || desc->structSize < offsetof(ShardColliderDesc, rotation) + sizeof(float) * 4) return SHARD_E_INVALID_ARG;
            if (!FiniteD(desc->position) || !FiniteQ(desc->rotation)) return SHARD_E_INVALID_ARG;
            std::string why;
            shape = BuildShape(*desc, why);
            if (!shape) { ctx.Error("collider : " + why); return SHARD_E_INVALID_ARG; }
            return SHARD_OK;
        }

        void HookRelease(PluginContext& ctx, PluginOwner owner)
        {
            {
                std::lock_guard<std::mutex> lock(gReleaseMutex);
                if (gReleaseHooked[owner]) return;
                gReleaseHooked[owner] = true;
            }
            ctx.OnRelease("physics-colliders", [owner] {
                for (auto& c : gColliders.RemoveOwner(owner)) {
                    std::lock_guard<std::mutex> lock(gColliderMutex);
                    c->destroyed = true;
                    gToApply.push_back(c);
                }
                {
                    std::lock_guard<std::mutex> lock(gReleaseMutex);
                    gReleaseHooked.erase(owner);
                }
                PhysicsBeginFrame();       // the bodies leave the world now, not at the next frame : the plugin may be about to go
            });
        }

        ShardResult SHARD_CALL ColliderCreate(ShardPluginCtx handle, const ShardColliderDesc* desc, ShardCollider* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!Manager()) return SHARD_E_UNSUPPORTED;
                JPH::RefConst<JPH::Shape> shape;
                const ShardResult r = ColliderDescriptor(*ctx, desc, shape);
                if (r != SHARD_OK) return r;

                auto collider = std::make_shared<Collider>();
                collider->plugin = PluginOwner(handle);
                collider->kind = desc->kind;
                collider->shape = shape;
                collider->owner = desc->owner;
                collider->userData = desc->userData;
                collider->friction = std::isfinite(desc->friction) ? std::max(desc->friction, 0.0f) : 0.5f;
                collider->restitution = std::isfinite(desc->restitution) ? std::clamp(desc->restitution, 0.0f, 1.0f) : 0.0f;
                collider->position = JPH::RVec3(desc->position[0], desc->position[1], desc->position[2]);
                const glm::quat q = glm::normalize(glm::quat(desc->rotation[3], desc->rotation[0], desc->rotation[1], desc->rotation[2]));
                collider->rotation = JPH::Quat(q.x, q.y, q.z, q.w);
                collider->handle = gColliders.Add(PluginOwner(handle), collider);
                HookRelease(*ctx, PluginOwner(handle));
                {
                    std::lock_guard<std::mutex> lock(gColliderMutex);
                    gToApply.push_back(collider);
                }
                *out = collider->handle;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ColliderSetPose(ShardPluginCtx handle, ShardCollider collider, const double position[3], const float rotation[4])
        {
            try {
                if (!position || !rotation || !FiniteD(position) || !FiniteQ(rotation)) return SHARD_E_INVALID_ARG;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                std::shared_ptr<Collider> c = gColliders.Get(PluginOwner(handle), collider);
                if (!c) return SHARD_E_STALE_HANDLE;
                const glm::quat q = glm::normalize(glm::quat(rotation[3], rotation[0], rotation[1], rotation[2]));
                std::lock_guard<std::mutex> lock(gColliderMutex);
                c->position = JPH::RVec3(position[0], position[1], position[2]);
                c->rotation = JPH::Quat(q.x, q.y, q.z, q.w);
                if (!c->poseDirty) { c->poseDirty = true; gToApply.push_back(c); }
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ColliderUpdate(ShardPluginCtx handle, ShardCollider collider, const ShardColliderDesc* desc)
        {
            try {
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                std::shared_ptr<Collider> c = gColliders.Get(PluginOwner(handle), collider);
                if (!c) return SHARD_E_STALE_HANDLE;
                JPH::RefConst<JPH::Shape> shape;
                const ShardResult r = ColliderDescriptor(*ctx, desc, shape);
                if (r != SHARD_OK) return r;
                std::lock_guard<std::mutex> lock(gColliderMutex);
                c->shape = shape;
                c->kind = desc->kind;
                if (!c->shapeDirty) { c->shapeDirty = true; gToApply.push_back(c); }
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ColliderDestroy(ShardPluginCtx handle, ShardCollider collider)
        {
            try {
                std::shared_ptr<Collider> c;
                if (!gColliders.Remove(PluginOwner(handle), collider, &c)) return SHARD_E_STALE_HANDLE;
                std::lock_guard<std::mutex> lock(gColliderMutex);
                c->destroyed = true;
                gToApply.push_back(c);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // ---- Queries ----

        struct QueryFilter {
            uint32_t layers = SHARD_LAYER_ALL;
            uint32_t flags = 0;
            JPH::BodyID ignored;
            bool hasTag = false;
            Core::Tag tag;
        };

        bool ReadFilter(const ShardQueryFilter* filter, QueryFilter& out)
        {
            if (!filter) return true;
            if (filter->structSize < offsetof(ShardQueryFilter, tag)) return false;
            out.layers = filter->layers ? filter->layers : SHARD_LAYER_ALL;
            out.flags = filter->flags;
            if (filter->ignore) {
                std::shared_ptr<Objects::Actor> actor = ActorOf(filter->ignore);
                if (!actor) return false;
                for (const auto& body : actor->GetComponents<PhysicsBody>())
                    if (!body->GetBodyID().IsInvalid()) { out.ignored = body->GetBodyID(); break; }
            }
            if (filter->structSize >= sizeof(ShardQueryFilter) && filter->tag.size) {
                out.tag = Core::Tag::Request(ToString(filter->tag));
                out.hasTag = true;
            }
            return true;
        }

        // Which bodies a query looks at : the actors' and the plugin's, the ones the filter lets through
        class PluginBodyFilter final : public JPH::BodyFilter {
        public:
            PluginBodyFilter(const QueryFilter& f, Physics::PhysicsManager& m) : mFilter(f), mManager(m) {}
            bool ShouldCollide(const JPH::BodyID& id) const override
            {
                if (id == mFilter.ignored && !id.IsInvalid()) return false;
                PhysicsBody* component = mManager.ComponentOf(id);
                if (!component) return !(mFilter.flags & SHARD_QUERY_NO_COLLIDERS) && !mFilter.hasTag;
                if (mFilter.flags & SHARD_QUERY_COLLIDERS_ONLY) return false;
                if (mFilter.hasTag) return mFilter.tag.IsValid() && component->parent && component->parent->GetTags().HasTag(mFilter.tag);
                return true;
            }
        private:
            const QueryFilter& mFilter;
            Physics::PhysicsManager& mManager;
        };

        void LayerMasks(uint32_t layers, Physics::ObjectLayerMask& objectMask, Physics::BroadPhaseLayerMask& broadMask)
        {
            objectMask = 0;
            broadMask = 0;
            if (layers & SHARD_LAYER_STATIC) { objectMask |= Physics::ObjectLayer_NonMoving; broadMask |= Physics::BroadPhase_NonMoving; }
            if (layers & SHARD_LAYER_MOVING) { objectMask |= Physics::ObjectLayer_Moving; broadMask |= Physics::BroadPhase_Moving; }
        }

        // The hit written into the caller's struct : what fits its structSize
        void Store(const ShardRayHit& hit, ShardRayHit* out)
        {
            const uint32_t asked = out->structSize;
            std::memcpy(out, &hit, std::min<size_t>(asked, sizeof(ShardRayHit)));
            out->structSize = asked;
        }

        // The ShardRayHit of a body and a sub shape
        ShardRayHit Describe(Physics::PhysicsManager& manager, JPH::BodyID id, JPH::SubShapeID subShape)
        {
            ShardRayHit hit{};
            hit.structSize = sizeof(ShardRayHit);
            if (PhysicsBody* component = manager.ComponentOf(id)) {
                if (component->parent) hit.actor = ActorHandle(component->parent);
                return hit;
            }
            JPH::BodyLockRead lock(manager.GetPhysicsSystem().GetBodyLockInterface(), id);
            if (!lock.Succeeded()) return hit;
            const JPH::Body& body = lock.GetBody();
            hit.collider = body.GetUserData();
            std::shared_ptr<Collider> c = gColliders.GetAny(hit.collider);
            if (!c) { hit.collider = 0; return hit; }
            hit.userData = c->userData;
            hit.actor = c->owner && ActorOf(c->owner) ? c->owner : 0;
            const JPH::Shape* shape = body.GetShape();
            if (shape->GetSubType() == JPH::EShapeSubType::Mesh) {
                JPH::SubShapeID remainder;
                hit.triangle = static_cast<const JPH::MeshShape*>(shape)->GetTriangleUserData(subShape);
            } else if (shape->GetType() == JPH::EShapeType::Compound) {
                JPH::SubShapeID remainder;
                hit.box = static_cast<const JPH::CompoundShape*>(shape)->GetSubShapeIndexFromID(subShape, remainder);
            }
            return hit;
        }

        struct Prepared {
            Physics::PhysicsManager* manager = nullptr;
            QueryFilter filter;
            Physics::ObjectLayerMask objectMask = 0;
            Physics::BroadPhaseLayerMask broadMask = 0;
        };

        ShardResult Prepare(const ShardQueryFilter* filter, Prepared& p)
        {
            if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
            p.manager = Manager();
            if (!p.manager) return SHARD_E_UNSUPPORTED;
            if (!ReadFilter(filter, p.filter)) return SHARD_E_INVALID_ARG;
            LayerMasks(p.filter.layers, p.objectMask, p.broadMask);
            return SHARD_OK;
        }

        // The `capacity` first hits written, how many there are in *count
        void StoreMany(const std::vector<ShardRayHit>& hits, ShardRayHit* out, uint32_t capacity, uint32_t* count)
        {
            *count = uint32_t(hits.size());
            for (uint32_t i = 0; i < capacity && i < hits.size(); ++i) {
                if (out[i].structSize < offsetof(ShardRayHit, distance) + sizeof(float)) continue;
                Store(hits[i], &out[i]);
            }
        }

        bool ValidOut(ShardRayHit* out, uint32_t capacity, uint32_t* count)
        {
            if (!count || (capacity && !out)) return false;
            for (uint32_t i = 0; i < capacity; ++i) if (out[i].structSize < offsetof(ShardRayHit, distance) + sizeof(float)) return false;
            return true;
        }

        ShardResult SHARD_CALL Raycast(const float origin[3], const float direction[3], float maxDistance, uint32_t layers, ShardActor ignore, ShardRayHit* out)
        {
            try {
                if (!origin || !direction || !out || !Finite(origin) || !Finite(direction) || !(maxDistance > 0.0f) || !std::isfinite(maxDistance)) return SHARD_E_INVALID_ARG;
                if (out->structSize < offsetof(ShardRayHit, distance) + sizeof(float)) return SHARD_E_INVALID_ARG;
                const glm::vec3 d = Vec(direction);
                if (glm::dot(d, d) < 1e-12f) return SHARD_E_INVALID_ARG;
                ShardQueryFilter filter{};
                filter.structSize = sizeof(ShardQueryFilter);
                filter.layers = layers;
                filter.ignore = ignore;
                Prepared p;
                const ShardResult prepared = Prepare(&filter, p);
                if (prepared != SHARD_OK) return prepared;
                if (!p.objectMask) return SHARD_E_NOT_FOUND;

                const glm::vec3 n = glm::normalize(d);
                const JPH::RRayCast ray(JPH::RVec3(origin[0], origin[1], origin[2]), JPH::Vec3(n.x, n.y, n.z) * maxDistance);
                Physics::RaycastBroadPhaseFilter broad(p.broadMask);
                Physics::RaycastObjectLayerFilter object(p.objectMask);
                PluginBodyFilter bodies(p.filter, *p.manager);
                JPH::RayCastResult hit;
                if (!p.manager->GetPhysicsSystem().GetNarrowPhaseQuery().CastRay(ray, hit, broad, object, bodies)) return SHARD_E_NOT_FOUND;

                ShardRayHit result = Describe(*p.manager, hit.mBodyID, hit.mSubShapeID2);
                if (!result.actor && !result.collider) return SHARD_E_NOT_FOUND;
                const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
                result.point[0] = float(point.GetX()); result.point[1] = float(point.GetY()); result.point[2] = float(point.GetZ());
                JPH::BodyLockRead lock(p.manager->GetPhysicsSystem().GetBodyLockInterface(), hit.mBodyID);
                if (lock.Succeeded()) {
                    const JPH::Vec3 normal = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point);
                    result.normal[0] = normal.GetX(); result.normal[1] = normal.GetY(); result.normal[2] = normal.GetZ();
                }
                result.distance = hit.mFraction * maxDistance;
                Store(result, out);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL RaycastAll(const float origin[3], const float direction[3], float maxDistance, const ShardQueryFilter* filter,
                                          ShardRayHit* out, uint32_t capacity, uint32_t* count)
        {
            try {
                if (!origin || !direction || !Finite(origin) || !Finite(direction) || !(maxDistance > 0.0f) || !std::isfinite(maxDistance) || !ValidOut(out, capacity, count)) return SHARD_E_INVALID_ARG;
                *count = 0;
                const glm::vec3 d = Vec(direction);
                if (glm::dot(d, d) < 1e-12f) return SHARD_E_INVALID_ARG;
                Prepared p;
                const ShardResult prepared = Prepare(filter, p);
                if (prepared != SHARD_OK) return prepared;
                if (!p.objectMask) return SHARD_OK;

                const glm::vec3 n = glm::normalize(d);
                const JPH::RRayCast ray(JPH::RVec3(origin[0], origin[1], origin[2]), JPH::Vec3(n.x, n.y, n.z) * maxDistance);
                Physics::RaycastBroadPhaseFilter broad(p.broadMask);
                Physics::RaycastObjectLayerFilter object(p.objectMask);
                PluginBodyFilter bodies(p.filter, *p.manager);
                JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
                p.manager->GetPhysicsSystem().GetNarrowPhaseQuery().CastRay(ray, JPH::RayCastSettings(), collector, broad, object, bodies);
                collector.Sort();

                std::vector<ShardRayHit> hits;
                for (const JPH::RayCastResult& hit : collector.mHits) {
                    ShardRayHit result = Describe(*p.manager, hit.mBodyID, hit.mSubShapeID2);
                    if (!result.actor && !result.collider) continue;
                    const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
                    result.point[0] = float(point.GetX()); result.point[1] = float(point.GetY()); result.point[2] = float(point.GetZ());
                    JPH::BodyLockRead lock(p.manager->GetPhysicsSystem().GetBodyLockInterface(), hit.mBodyID);
                    if (lock.Succeeded()) {
                        const JPH::Vec3 normal = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point);
                        result.normal[0] = normal.GetX(); result.normal[1] = normal.GetY(); result.normal[2] = normal.GetZ();
                    }
                    result.distance = hit.mFraction * maxDistance;
                    hits.push_back(result);
                }
                StoreMany(hits, out, capacity, count);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // The bodies that touch `shape` placed at `transform`
        ShardResult OverlapShape(const JPH::Shape* shape, JPH::RMat44Arg transform, const ShardQueryFilter* filter, ShardRayHit* out, uint32_t capacity, uint32_t* count)
        {
            if (!ValidOut(out, capacity, count)) return SHARD_E_INVALID_ARG;
            *count = 0;
            Prepared p;
            const ShardResult prepared = Prepare(filter, p);
            if (prepared != SHARD_OK) return prepared;
            if (!p.objectMask) return SHARD_OK;

            Physics::RaycastBroadPhaseFilter broad(p.broadMask);
            Physics::RaycastObjectLayerFilter object(p.objectMask);
            PluginBodyFilter bodies(p.filter, *p.manager);
            JPH::CollideShapeSettings settings;
            const JPH::RVec3 base = transform.GetTranslation();
            JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
            p.manager->GetPhysicsSystem().GetNarrowPhaseQuery().CollideShape(shape, JPH::Vec3::sReplicate(1.0f), transform, settings, base, collector, broad, object, bodies);

            // A body is reported once, with its deepest contact ; ordered by body so that the same world gives the same list
            std::unordered_map<uint32_t, JPH::CollideShapeResult> deepest;
            for (const JPH::CollideShapeResult& r : collector.mHits) {
                auto it = deepest.find(r.mBodyID2.GetIndexAndSequenceNumber());
                if (it == deepest.end() || r.mPenetrationDepth > it->second.mPenetrationDepth) deepest[r.mBodyID2.GetIndexAndSequenceNumber()] = r;
            }
            std::vector<JPH::CollideShapeResult> ordered;
            for (auto& [key, r] : deepest) ordered.push_back(r);
            std::sort(ordered.begin(), ordered.end(), [](const JPH::CollideShapeResult& a, const JPH::CollideShapeResult& b) {
                return a.mBodyID2.GetIndexAndSequenceNumber() < b.mBodyID2.GetIndexAndSequenceNumber();
            });

            std::vector<ShardRayHit> hits;
            for (const JPH::CollideShapeResult& r : ordered) {
                ShardRayHit result = Describe(*p.manager, r.mBodyID2, r.mSubShapeID2);
                if (!result.actor && !result.collider) continue;
                const JPH::RVec3 point = base + JPH::RVec3(r.mContactPointOn2);
                result.point[0] = float(point.GetX()); result.point[1] = float(point.GetY()); result.point[2] = float(point.GetZ());
                // Pointing from the body toward the shape : the way to push the shape out
                const JPH::Vec3 axis = -r.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY());
                result.normal[0] = axis.GetX(); result.normal[1] = axis.GetY(); result.normal[2] = axis.GetZ();
                result.distance = r.mPenetrationDepth;
                hits.push_back(result);
            }
            StoreMany(hits, out, capacity, count);
            return SHARD_OK;
        }

        JPH::Quat RotationOf(const float q[4])
        {
            if (!q) return JPH::Quat::sIdentity();
            const glm::quat n = glm::normalize(glm::quat(q[3], q[0], q[1], q[2]));
            return JPH::Quat(n.x, n.y, n.z, n.w);
        }

        ShardResult SHARD_CALL OverlapSphere(const float center[3], float radius, const ShardQueryFilter* filter, ShardRayHit* out, uint32_t capacity, uint32_t* count)
        {
            try {
                if (!center || !Finite(center) || !(radius > 0.0f) || !std::isfinite(radius)) return SHARD_E_INVALID_ARG;
                JPH::RefConst<JPH::Shape> shape = new JPH::SphereShape(radius);
                return OverlapShape(shape, JPH::RMat44::sTranslation(JPH::RVec3(center[0], center[1], center[2])), filter, out, capacity, count);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL OverlapBox(const float center[3], const float half[3], const float rotation[4], const ShardQueryFilter* filter, ShardRayHit* out, uint32_t capacity, uint32_t* count)
        {
            try {
                if (!center || !half || !Finite(center) || !Finite(half) || (rotation && !FiniteQ(rotation))) return SHARD_E_INVALID_ARG;
                const JPH::Vec3 h(std::max(half[0], 0.001f), std::max(half[1], 0.001f), std::max(half[2], 0.001f));
                JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(h, std::min(0.05f, std::min(h.GetX(), std::min(h.GetY(), h.GetZ()))));
                return OverlapShape(shape, JPH::RMat44::sRotationTranslation(RotationOf(rotation), JPH::RVec3(center[0], center[1], center[2])), filter, out, capacity, count);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // A capsule between two points : Jolt's stands along Y, around its centre
        bool CapsuleOf(const float from[3], const float to[3], float radius, JPH::RefConst<JPH::Shape>& shape, JPH::RMat44& transform)
        {
            if (!from || !to || !Finite(from) || !Finite(to) || !(radius > 0.0f) || !std::isfinite(radius)) return false;
            const JPH::Vec3 a(from[0], from[1], from[2]), b(to[0], to[1], to[2]);
            const JPH::Vec3 axis = b - a;
            const float length = axis.Length();
            shape = new JPH::CapsuleShape(std::max(length * 0.5f, 0.001f), radius);
            JPH::Quat rotation = JPH::Quat::sIdentity();
            if (length > 1e-6f) {
                const JPH::Vec3 up = JPH::Vec3::sAxisY(), direction = axis / length;
                const float d = up.Dot(direction);
                if (d < -0.9999f) rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), JPH_PI);
                else if (d < 0.9999f) rotation = JPH::Quat::sRotation(up.Cross(direction).Normalized(), std::acos(std::clamp(d, -1.0f, 1.0f)));
            }
            const JPH::Vec3 mid = (a + b) * 0.5f;
            transform = JPH::RMat44::sRotationTranslation(rotation, JPH::RVec3(mid));
            return true;
        }

        ShardResult SHARD_CALL OverlapCapsule(const float from[3], const float to[3], float radius, const ShardQueryFilter* filter, ShardRayHit* out, uint32_t capacity, uint32_t* count)
        {
            try {
                JPH::RefConst<JPH::Shape> shape;
                JPH::RMat44 transform;
                if (!CapsuleOf(from, to, radius, shape, transform)) return SHARD_E_INVALID_ARG;
                return OverlapShape(shape, transform, filter, out, capacity, count);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // The first body a shape meets going along `direction`
        ShardResult SweepShape(const JPH::Shape* shape, JPH::RMat44Arg start, const float direction[3], float maxDistance, const ShardQueryFilter* filter, ShardRayHit* out)
        {
            if (!out || !direction || !Finite(direction) || !(maxDistance > 0.0f) || !std::isfinite(maxDistance)) return SHARD_E_INVALID_ARG;
            if (out->structSize < offsetof(ShardRayHit, distance) + sizeof(float)) return SHARD_E_INVALID_ARG;
            const glm::vec3 d = Vec(direction);
            if (glm::dot(d, d) < 1e-12f) return SHARD_E_INVALID_ARG;
            Prepared p;
            const ShardResult prepared = Prepare(filter, p);
            if (prepared != SHARD_OK) return prepared;
            if (!p.objectMask) return SHARD_E_NOT_FOUND;

            const glm::vec3 n = glm::normalize(d);
            const JPH::Vec3 move = JPH::Vec3(n.x, n.y, n.z) * maxDistance;
            const JPH::RShapeCast cast(shape, JPH::Vec3::sReplicate(1.0f), start, move);
            Physics::RaycastBroadPhaseFilter broad(p.broadMask);
            Physics::RaycastObjectLayerFilter object(p.objectMask);
            PluginBodyFilter bodies(p.filter, *p.manager);
            const JPH::RVec3 base = start.GetTranslation();
            JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
            p.manager->GetPhysicsSystem().GetNarrowPhaseQuery().CastShape(cast, JPH::ShapeCastSettings(), base, collector, broad, object, bodies);
            if (!collector.HadHit()) return SHARD_E_NOT_FOUND;

            const JPH::ShapeCastResult& r = collector.mHit;
            ShardRayHit result = Describe(*p.manager, r.mBodyID2, r.mSubShapeID2);
            if (!result.actor && !result.collider) return SHARD_E_NOT_FOUND;
            const JPH::RVec3 point = base + JPH::RVec3(r.mContactPointOn2);
            result.point[0] = float(point.GetX()); result.point[1] = float(point.GetY()); result.point[2] = float(point.GetZ());
            const JPH::Vec3 normal = -r.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY());
            result.normal[0] = normal.GetX(); result.normal[1] = normal.GetY(); result.normal[2] = normal.GetZ();
            result.distance = r.mFraction * maxDistance;
            Store(result, out);
            return SHARD_OK;
        }

        ShardResult SHARD_CALL SweepSphere(const float origin[3], float radius, const float direction[3], float maxDistance, const ShardQueryFilter* filter, ShardRayHit* out)
        {
            try {
                if (!origin || !Finite(origin) || !(radius > 0.0f) || !std::isfinite(radius)) return SHARD_E_INVALID_ARG;
                JPH::RefConst<JPH::Shape> shape = new JPH::SphereShape(radius);
                return SweepShape(shape, JPH::RMat44::sTranslation(JPH::RVec3(origin[0], origin[1], origin[2])), direction, maxDistance, filter, out);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SweepBox(const float origin[3], const float half[3], const float rotation[4], const float direction[3], float maxDistance, const ShardQueryFilter* filter, ShardRayHit* out)
        {
            try {
                if (!origin || !half || !Finite(origin) || !Finite(half) || (rotation && !FiniteQ(rotation))) return SHARD_E_INVALID_ARG;
                const JPH::Vec3 h(std::max(half[0], 0.001f), std::max(half[1], 0.001f), std::max(half[2], 0.001f));
                JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(h, std::min(0.05f, std::min(h.GetX(), std::min(h.GetY(), h.GetZ()))));
                return SweepShape(shape, JPH::RMat44::sRotationTranslation(RotationOf(rotation), JPH::RVec3(origin[0], origin[1], origin[2])), direction, maxDistance, filter, out);
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    // Applies what the plugins asked of their colliders since the last call : bodies appear, move, change shape and go. Main thread, between the
    // frames' simulations (the physics world does not accept changes while it steps).
    void PhysicsBeginFrame()
    {
        std::vector<std::shared_ptr<Collider>> work;
        {
            std::lock_guard<std::mutex> lock(gColliderMutex);
            work.swap(gToApply);
        }
        if (work.empty()) return;
        Physics::PhysicsManager* physics = Manager();
        if (!physics || !physics->IsInitialized()) return;         // after Shutdown() the bodies went with the system
        JPH::BodyInterface& bodies = physics->GetBodyInterface();
        for (const std::shared_ptr<Collider>& c : work) {
            std::lock_guard<std::mutex> lock(gColliderMutex);
            if (c->destroyed) {
                if (!c->body.IsInvalid()) {
                    physics->RemoveBody(c->body);
                    c->body = JPH::BodyID();
                }
                c->shape = nullptr;
                continue;
            }
            if (c->body.IsInvalid()) {
                if (!c->shape) continue;
                JPH::BodyCreationSettings settings(c->shape, c->position, c->rotation, JPH::EMotionType::Static, Physics::Layers::NON_MOVING);
                settings.mFriction = c->friction;
                settings.mRestitution = c->restitution;
                settings.mUserData = c->handle;
                c->body = physics->CreateBody(settings, nullptr, JPH::EActivation::DontActivate);
                c->shape = nullptr;
                c->shapeDirty = c->poseDirty = false;
                continue;
            }
            if (c->shapeDirty && c->shape) {
                bodies.SetShape(c->body, c->shape, false, JPH::EActivation::DontActivate);
                c->shape = nullptr;
            }
            if (c->poseDirty) bodies.SetPositionAndRotation(c->body, c->position, c->rotation, JPH::EActivation::DontActivate);
            c->shapeDirty = c->poseDirty = false;
        }
    }

    namespace {
        ContactInfo Decode(const Objects::Components::PhysicsBody& other, const JPH::ContactManifold* manifold)
        {
            ContactInfo info;
            info.other = other.parent;
            if (!manifold) return info;
            info.hasManifold = true;
            info.normal[0] = manifold->mWorldSpaceNormal.GetX();
            info.normal[1] = manifold->mWorldSpaceNormal.GetY();
            info.normal[2] = manifold->mWorldSpaceNormal.GetZ();
            if (!manifold->mRelativeContactPointsOn1.empty()) {
                const JPH::RVec3 point = manifold->GetWorldSpaceContactPointOn1(0);
                info.hasPoint = true;
                info.point[0] = float(point.GetX());
                info.point[1] = float(point.GetY());
                info.point[2] = float(point.GetZ());
            }
            info.penetration = manifold->mPenetrationDepth;
            return info;
        }
    }

    ContactInfo DecodeContact(const Events::ContactAddedEvent& e) { return Decode(e.otherBody, &e.contactManifold); }
    ContactInfo DecodeContact(const Events::ContactPersistedEvent& e) { return Decode(e.otherBody, &e.contactManifold); }
    ContactInfo DecodeContact(const Events::ContactRemovedEvent& e) { return Decode(e.otherBody, nullptr); }

    const void* PhysicsApi()
    {
        static const ShardPhysicsApi api = {
            sizeof(ShardPhysicsApi),
            &GetVelocity, &SetVelocity, &GetAngularVelocity, &SetAngularVelocity,
            &AddForce, &AddForceAtPoint, &AddTorque, &AddImpulse, &AddImpulseAtPoint, &AddAngularImpulse,
            &GetGravity, &SetCustomGravity, &Raycast,
            &RaycastAll, &OverlapSphere, &OverlapBox, &OverlapCapsule, &SweepSphere, &SweepBox,
            &ColliderCreate, &ColliderSetPose, &ColliderUpdate, &ColliderDestroy };
        return &api;
    }
}

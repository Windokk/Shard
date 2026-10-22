#include <cmath>

#include <glm/glm.hpp>

#include <shard/shard_physics.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyFilter.h>

#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_events.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"

// shard.physics : the PhysicsBody of an actor, and the physics manager's ray casts. Lives in the engine root, not in src/engine/plugins/abi with
// the other interfaces: the plugin layer is below physics and may not include it (scripts/check_deps.py), and c_internal.hpp declares what
// it needs from here (PhysicsApi, DecodeContact).

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        using Objects::Components::PhysicsBody;

        glm::vec3 Vec(const float v[3]) { return glm::vec3(v[0], v[1], v[2]); }
        void Out(const glm::vec3& v, float out[3]) { out[0] = v.x; out[1] = v.y; out[2] = v.z; }
        bool Finite(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

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

        ShardResult SHARD_CALL Raycast(const float origin[3], const float direction[3], float maxDistance, uint32_t layers, ShardActor ignore, ShardRayHit* out)
        {
            try {
                if (!origin || !direction || !out || !Finite(origin) || !Finite(direction) || !(maxDistance > 0.0f) || !std::isfinite(maxDistance)) return SHARD_E_INVALID_ARG;
                if (out->structSize < sizeof(ShardRayHit)) return SHARD_E_INVALID_ARG;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                const glm::vec3 d = Vec(direction);
                if (glm::dot(d, d) < 1e-12f) return SHARD_E_INVALID_ARG;
                Core::IEngineContext* engine = Core::gSharedEnginePtr;
                Physics::PhysicsManager* physics = engine ? engine->GetPhysicsManager() : nullptr;
                if (!physics) return SHARD_E_UNSUPPORTED;

                Physics::RaycastRequest request;
                request.origin = Vec(origin);
                request.direction = glm::normalize(d);
                request.maxDistance = maxDistance;
                request.objectMask = 0;
                request.broadPhaseMask = 0;
                if (layers & SHARD_LAYER_STATIC) { request.objectMask |= Physics::ObjectLayer_NonMoving; request.broadPhaseMask |= Physics::BroadPhase_NonMoving; }
                if (layers & SHARD_LAYER_MOVING) { request.objectMask |= Physics::ObjectLayer_Moving; request.broadPhaseMask |= Physics::BroadPhase_Moving; }
                if (!request.objectMask) return SHARD_E_NOT_FOUND;

                JPH::BodyID ignored;
                if (ignore) {
                    std::shared_ptr<Objects::Actor> actor = ActorOf(ignore);
                    if (!actor) return SHARD_E_STALE_HANDLE;
                    for (const auto& body : actor->GetComponents<PhysicsBody>())
                        if (!body->GetBodyID().IsInvalid()) { ignored = body->GetBodyID(); break; }
                }
                JPH::IgnoreSingleBodyFilter filter(ignored);
                if (!ignored.IsInvalid()) request.bodyFilter = &filter;

                const Physics::RaycastResult hit = physics->RayCast(request);
                if (!hit.hit || !hit.hitBody || !hit.hitBody->parent) return SHARD_E_NOT_FOUND;
                out->actor = ActorHandle(hit.hitBody->parent);
                Out(hit.point, out->point);
                Out(hit.normal, out->normal);
                out->distance = hit.hitDistance;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
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
            &GetGravity, &SetCustomGravity, &Raycast };
        return &api;
    }
}

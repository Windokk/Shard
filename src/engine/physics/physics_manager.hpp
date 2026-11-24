#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>

#include <iostream>
#include <cstdarg>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <memory>
#include <vector>
#include <functional>

#include "engine/core/color.hpp"
#include <glm/gtc/quaternion.hpp>

#include "engine/core/diagnostics/logger.hpp"

#include "engine/physics/gravity.hpp"
#include "engine/physics/collision/collision_system.hpp"
#include "engine/physics/mesh/mesh_collider.hpp"



using namespace JPH;

using namespace JPH::literals;

using namespace std;

namespace Shard::Engine::Objects{
    class Actor;
}

namespace Shard::Engine::Objects::Components{
    class PhysicsBody;
    class PhysicsConstraint;
    class Script;
}

inline JPH::Quat ToJolt(const glm::quat& q)
{
    glm::quat n = glm::normalize(q);
    return JPH::Quat(n.x, n.y, n.z, n.w);
}

// Jolt vectors are float regardless of position precision.
inline JPH::Vec3 ToJolt(const glm::vec3& v)
{
    return JPH::Vec3(v.x, v.y, v.z);
}

inline glm::quat ToGLM(const JPH::Quat& q)
{
    return glm::quat(
        q.GetW(),
        q.GetX(),
        q.GetY(),
        q.GetZ()
    );
}

// Jolt positions are double (JPH_DOUBLE_PRECISION).
inline JPH::RVec3 ToJolt(const glm::dvec3& v)
{
    return JPH::RVec3(v.x, v.y, v.z);
}

inline JPH::Quat ToJolt(const glm::dquat& q)
{
    const glm::dquat n = glm::normalize(q);
    return JPH::Quat(float(n.x), float(n.y), float(n.z), float(n.w));
}

inline glm::dvec3 ToGLMD(const JPH::RVec3& v)
{
    return glm::dvec3(double(v.GetX()), double(v.GetY()), double(v.GetZ()));
}

inline glm::vec3 ToGLM(const JPH::Vec3& v)
{
    return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

#ifdef JPH_DOUBLE_PRECISION
inline glm::vec3 ToGLM(const JPH::RVec3& v)
{
    return glm::vec3(
        static_cast<float>(v.GetX()),
        static_cast<float>(v.GetY()),
        static_cast<float>(v.GetZ())
    );
}
#endif

namespace Shard::Engine::Physics
{
    
    enum PhysicsShape{
        SPHERE,
        BOX,
        CAPSULE,
        CYLINDER,
        MESH
    };

    inline void TraceImpl(const char *inFMT, ...)
    {
        va_list list;
        va_start(list, inFMT);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), inFMT, list);
        va_end(list);

        cout << buffer << endl;
    }

    #ifdef JPH_ENABLE_ASSERTS

    inline bool AssertFailedImpl(const char *inExpression, const char *inMessage, const char *inFile, uint inLine)
    {
        cout << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr? inMessage : "") << endl;

        return true;
    };

    #endif


    namespace Layers
    {
        static constexpr ObjectLayer NON_MOVING = 0;
        static constexpr ObjectLayer MOVING = 1;
    };

    namespace BroadPhaseLayers
    {
        static constexpr uint NON_MOVING_IDX = 0;
        static constexpr uint MOVING_IDX     = 1;

        static constexpr BroadPhaseLayer NON_MOVING(NON_MOVING_IDX);
        static constexpr BroadPhaseLayer MOVING(MOVING_IDX);

        static constexpr uint NUM_LAYERS = 2;
    }

    class PhysicsBodyActivationListener : public BodyActivationListener
    {
    public:
        virtual void OnBodyActivated(const BodyID &inBodyID, uint64 inBodyUserData) override
        {
        }

        virtual void OnBodyDeactivated(const BodyID &inBodyID, uint64 inBodyUserData) override
        {
        }
    };

    class PhysicsManager;

    class PhysicsContactListener : public ContactListener
    {
    public:
        PhysicsManager* owner = nullptr;

        virtual ValidateResult OnContactValidate(const Body &inBody1, const Body &inBody2, RVec3Arg inBaseOffset, const CollideShapeResult &inCollisionResult) override;

        virtual void OnContactAdded(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings) override;

        virtual void OnContactPersisted(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings) override;

        virtual void OnContactRemoved(const SubShapeIDPair &inSubShapePair) override;
    };
    
    using ObjectLayerMask = uint32_t;
    using BroadPhaseLayerMask = uint32_t;

    constexpr ObjectLayerMask ObjectLayer_NonMoving = 1u;
    constexpr ObjectLayerMask ObjectLayer_Moving    = 2u;

    constexpr BroadPhaseLayerMask BroadPhase_NonMoving =
        1u << BroadPhaseLayers::NON_MOVING_IDX;

    constexpr BroadPhaseLayerMask BroadPhase_Moving =
        1u << BroadPhaseLayers::MOVING_IDX;

    class RaycastBroadPhaseFilter final : public BroadPhaseLayerFilter
    {
    public:
        explicit RaycastBroadPhaseFilter(BroadPhaseLayerMask mask)
            : mMask(mask) {}

        bool ShouldCollide(BroadPhaseLayer inLayer) const override
        {
            return (mMask & (1u << inLayer.GetValue())) != 0;
        }

    private:
        BroadPhaseLayerMask mMask;
    };

    using RaycastObjectLayerFilter = CollisionSystem::MaskFilter;

    struct RaycastRequest
    {
        glm::vec3 origin;
        glm::vec3 direction;
        float maxDistance;

        ObjectLayerMask objectMask = ObjectLayer_Moving | ObjectLayer_NonMoving;
        BroadPhaseLayerMask broadPhaseMask = BroadPhase_Moving | BroadPhase_NonMoving;

        const BodyFilter* bodyFilter = nullptr;
        const ShapeFilter* shapeFilter = nullptr;
    };

    struct RaycastResult
    {
        bool hit = false;
        Objects::Components::PhysicsBody* hitBody = nullptr;
        float hitDistance = -1.0f;
        glm::vec3 point = glm::vec3(0.0f);
        glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
        bool onMesh = false;
        uint32_t submesh = 0;
    };

    class PhysicsManager {
    public:

        // `deterministic`: simulate on the calling thread. Results match on any thread count but contact callback order does not.
        void Init(glm::vec3 gravity, bool deterministic = false);
        void Shutdown();

        RaycastResult RayCast(RaycastRequest request);

        Objects::Components::PhysicsBody* ComponentOf(JPH::BodyID id) const { return FindComponent(id); }

        // World units per second squared. Scale it if a unit is not a metre.
        void SetGravity(glm::vec3 gravity);
        glm::vec3 GetGravity();

        // Jolt's solver : iterations of the velocity and position constraints per step, and the collision sub-steps each step is cut into (the quality presets set them).
        void SetSolverSettings(uint32_t velocitySteps, uint32_t positionSteps, uint32_t collisionSteps);

        // Jolt has one gravity. Fields and per-body gravity are applied on top as a force (difference from Jolt's), so no fields means no cost.

        void SetGravityFields(std::vector<GravityFieldData> fields);

        void CollectGravityFields();

        const std::vector<GravityFieldData>& GetGravityFields() const { return m_GravityFields; }

        glm::vec3 GetGravityAt(const glm::dvec3& position);

        void SetBodyGravity(JPH::BodyID id, GravityMode mode, glm::vec3 custom);

        glm::vec3 GetBodyGravity(JPH::BodyID id);

        void StepSimulation(float deltaTime, bool activateAll);

        uint64_t GetStepCount() const { return m_StepCount; }

        void TickBodies(float deltaTime);

        JPH::BodyID CreateBody(const JPH::BodyCreationSettings& settings, Objects::Components::PhysicsBody* component, JPH::EActivation activation = JPH::EActivation::Activate);
        JPH::BodyID CreateBody(const JPH::BodyCreationSettings& settings, Objects::Components::PhysicsBody* component, ColliderState&& collider, JPH::EActivation activation = JPH::EActivation::Activate);
        void RemoveBody(JPH::BodyID id, Collision::EndCause cause = Collision::EndCause::Destroyed);

        MeshColliderCache& MeshColliders() { return m_MeshColliders; }
        const MeshColliderCache& MeshColliders() const { return m_MeshColliders; }

        CollisionSystem& Collision() { return m_Collision; }
        const CollisionSystem& Collision() const { return m_Collision; }

        void RegisterConstraint(Objects::Components::PhysicsConstraint* owner, JPH::BodyID a, JPH::BodyID b, JPH::TwoBodyConstraint* constraint);
        void UnregisterConstraint(Objects::Components::PhysicsConstraint* owner);
        void TickConstraints(bool playing);
        void ClearConstraints();
        void WakeConstraintBodies(JPH::BodyID a, JPH::BodyID b);
        size_t GetConstraintCount() const { return m_Constraints.size(); }

        bool IsInitialized() const { return initialized; }

        JPH::BodyInterface& GetBodyInterface() {
            if(!initialized)
                DEBUG_FATAL("Physics system is not yet initialized");
            return m_physicsSystem.GetBodyInterface();
        }

        JPH::PhysicsSystem& GetPhysicsSystem() {
            if(!initialized)
                DEBUG_FATAL("Physics system is not yet initialized");
            return m_physicsSystem;
        }
        
    private:

        struct PendingContact {
            enum class Kind : uint8_t { Added, Persisted, Removed };
            Kind kind = Kind::Added;
            JPH::BodyID body1, body2;
            JPH::SubShapeID subShape1, subShape2;
            ContactManifold manifold;
            ContactSettings settings;
        };

        void ApplyGravityFields();

        void WakeBodiesOfChangedGravity();

        glm::vec3 BodyGravityAt(JPH::BodyID id, const glm::dvec3& position, const glm::vec3& world) const;

        void QueueContact(PendingContact::Kind kind, const Body &body1, const Body &body2, const ContactManifold &contactManifold, const ContactSettings &contactSettings);
        void QueueContactRemoved(const SubShapeIDPair &pair);

        void DispatchContacts();
        void DispatchContact(const PendingContact& contact, JPH::BodyID selfID, JPH::BodyID otherID);
        Objects::Components::PhysicsBody* FindComponent(JPH::BodyID id) const;
        const std::vector<std::shared_ptr<Objects::Components::Script>>& ScriptsOf(Objects::Actor& actor);

        struct ConstraintRecord {
            Objects::Components::PhysicsConstraint* owner = nullptr;
            JPH::BodyID a;
            JPH::BodyID b;
            JPH::Ref<JPH::TwoBodyConstraint> constraint;
        };

        void DropConstraintsOf(JPH::BodyID id);
        void CheckConstraintBreaks(float deltaTime);

        std::vector<ConstraintRecord> m_Constraints;

        std::unordered_map<JPH::BodyID, Objects::Components::PhysicsBody*> bodyIDToComponentMap;

        std::vector<GravityFieldData> m_GravityFields;
        bool m_GravityFieldsChanged = false;
        std::unordered_map<JPH::BodyID, BodyGravity> m_BodyGravity;
        std::unordered_map<JPH::BodyID, glm::vec3> m_AppliedGravity;
        JPH::BodyIDVector m_ActiveBodies;

        std::mutex m_ContactMutex;
        std::vector<PendingContact> m_PendingContacts;
        std::vector<PendingContact> m_DispatchedContacts;
        std::vector<uint32_t> m_DispatchOrder;
        std::unordered_map<Objects::Actor*, std::vector<std::shared_ptr<Objects::Components::Script>>> m_ScriptCache;

        JPH::TempAllocatorImpl* m_tempAllocator;
        JPH::JobSystem* m_jobSystem;

        JPH::PhysicsSystem m_physicsSystem;
        uint m_bodiesAddedSinceOptimize = 0;

        CollisionSystem m_Collision;
        MeshColliderCache m_MeshColliders;

        PhysicsContactListener m_contactListener;
        PhysicsBodyActivationListener m_activationListener;

        std::queue<std::function<void()>> mBodyUpdateQueue;
        std::mutex mQueueMutex;

        bool initialized = false;

        uint64_t m_StepCount = 0;
        int m_CollisionSteps = 1;

        friend class PhysicsContactListener;
        friend class CollisionSystem;
    };

}
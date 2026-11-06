#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSettings.h>
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



using namespace JPH;

using namespace JPH::literals;

using namespace std;

namespace Shard::Engine::Objects{
    class Actor;
}

namespace Shard::Engine::Objects::Components{
    class PhysicsBody;
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
        CYLINDER
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
        static constexpr ObjectLayer NUM_LAYERS = 2;
    };

    class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter
    {
    public:
        virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override
        {
            switch (inObject1)
            {
            case Layers::NON_MOVING:
                return inObject2 == Layers::MOVING;
            case Layers::MOVING:
                return true;
            default:
                JPH_ASSERT(false);
                return false;
            }
        }
    };

    namespace BroadPhaseLayers
    {
        static constexpr uint NON_MOVING_IDX = 0;
        static constexpr uint MOVING_IDX     = 1;

        static constexpr BroadPhaseLayer NON_MOVING(NON_MOVING_IDX);
        static constexpr BroadPhaseLayer MOVING(MOVING_IDX);

        static constexpr uint NUM_LAYERS = 2;
    }
    
    class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface
    {
    public:
        BPLayerInterfaceImpl()
        {
            mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
            mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
        }

        virtual uint GetNumBroadPhaseLayers() const override
        {
            return BroadPhaseLayers::NUM_LAYERS;
        }

        virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override
        {
            JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
            return mObjectToBroadPhase[inLayer];
        }

    #if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        virtual const char *			GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override
        {
            switch ((BroadPhaseLayer::Type)inLayer)
            {
            case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:	return "NON_MOVING";
            case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:		return "MOVING";
            default:													JPH_ASSERT(false); return "INVALID";
            }
        }
    #endif

    private:
        BroadPhaseLayer	mObjectToBroadPhase[Layers::NUM_LAYERS];
    };

    class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter
    {
    public:
        virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override
        {
            switch (inLayer1)
            {
            case Layers::NON_MOVING:
                return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING:
                return true;
            default:
                JPH_ASSERT(false);
                return false;
            }
        }
    };

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

    constexpr ObjectLayerMask ObjectLayer_NonMoving = 1 << Layers::NON_MOVING;
    constexpr ObjectLayerMask ObjectLayer_Moving    = 1 << Layers::MOVING;

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

    class RaycastObjectLayerFilter final : public ObjectLayerFilter
    {
    public:
        explicit RaycastObjectLayerFilter(ObjectLayerMask mask)
            : mMask(mask) {}

        bool ShouldCollide(ObjectLayer inLayer) const override
        {
            return (mMask & (1u << inLayer)) != 0;
        }

    private:
        ObjectLayerMask mMask;
    };

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
        void RemoveBody(JPH::BodyID id);

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

        ObjectLayerPairFilterImpl m_objectLayerFilter;
        ObjectVsBroadPhaseLayerFilterImpl m_objectVsBroadphase;
        BPLayerInterfaceImpl m_broadPhaseLayer;

        PhysicsContactListener m_contactListener;
        PhysicsBodyActivationListener m_activationListener;

        std::queue<std::function<void()>> mBodyUpdateQueue;
        std::mutex mQueueMutex;

        bool initialized = false;

        uint64_t m_StepCount = 0;

        friend class PhysicsContactListener;
    };

}
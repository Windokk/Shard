#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
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
#include <functional>

#include "engine/core/color.hpp"
#include <glm/gtc/quaternion.hpp>

#include "engine/core/diagnostics/logger.hpp"



using namespace JPH;

using namespace JPH::literals;

using namespace std;

namespace Shard::Engine::Objects::Components{
    class PhysicsBody;
}

static JPH::Quat ToJolt(const glm::quat& q)
{
    glm::quat n = glm::normalize(q);
    return JPH::Quat(n.x, n.y, n.z, n.w);
}

static JPH::RVec3 ToJolt(const glm::vec3& v)
{
    return JPH::RVec3(v.x, v.y, v.z);
}

static glm::quat ToGLM(const JPH::Quat& q)
{
    return glm::quat(
        q.GetW(),
        q.GetX(),
        q.GetY(),
        q.GetZ()
    );
}

static glm::vec3 ToGLM(const JPH::RVec3& v)
{
    return glm::vec3(
        static_cast<float>(v.GetX()),
        static_cast<float>(v.GetY()),
        static_cast<float>(v.GetZ())
    );
}

namespace Shard::Engine::Physics
{
    
    enum PhysicsShape{
        SPHERE,
        BOX,
        CAPSULE,
        CYLINDER
    };

    static void TraceImpl(const char *inFMT, ...)
    {
        // Format the message
        va_list list;
        va_start(list, inFMT);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), inFMT, list);
        va_end(list);

        // Print to the TTY
        cout << buffer << endl;
    }

    #ifdef JPH_ENABLE_ASSERTS

    // Callback for asserts, connect this to your own assert handler if you have one
    static bool AssertFailedImpl(const char *inExpression, const char *inMessage, const char *inFile, uint inLine)
    {
        // Print to the TTY
        cout << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr? inMessage : "") << endl;

        // Breakpoint
        return true;
    };

    #endif // JPH_ENABLE_ASSERTS


    namespace Layers
    {
        static constexpr ObjectLayer NON_MOVING = 0;
        static constexpr ObjectLayer MOVING = 1;
        static constexpr ObjectLayer NUM_LAYERS = 2;
    };

    /// Class that determines if two object layers can collide
    class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter
    {
    public:
        virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override
        {
            switch (inObject1)
            {
            case Layers::NON_MOVING:
                return inObject2 == Layers::MOVING; // Non moving only collides with moving
            case Layers::MOVING:
                return true; // Moving collides with everything
            default:
                JPH_ASSERT(false);
                return false;
            }
        }
    };

    // Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
    // a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
    // You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
    // many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
    // your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
    namespace BroadPhaseLayers
    {
        static constexpr uint NON_MOVING_IDX = 0;
        static constexpr uint MOVING_IDX     = 1;

        static constexpr BroadPhaseLayer NON_MOVING(NON_MOVING_IDX);
        static constexpr BroadPhaseLayer MOVING(MOVING_IDX);

        static constexpr uint NUM_LAYERS = 2;
    }
    
    // BroadPhaseLayerInterface implementation
    // This defines a mapping between object and broadphase layers.
    class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface
    {
    public:
        BPLayerInterfaceImpl()
        {
            // Create a mapping table from object to broad phase layer
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
    #endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

    private:
        BroadPhaseLayer	mObjectToBroadPhase[Layers::NUM_LAYERS];
    };

    /// Class that determines if an object layer can collide with a broadphase layer
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

    class PhysicsContactListener : public ContactListener
    {
    public:
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
    };

    class PhysicsManager {
    public:

        void Init(glm::vec3 gravity);
        void Shutdown();

        RaycastResult RayCast(RaycastRequest request);

        /// World units per second squared. Expressed in the project's own units, so a project where
        /// one unit isn't one metre needs this scaled accordingly rather than left at 9.81.
        void SetGravity(glm::vec3 gravity);
        glm::vec3 GetGravity();

        void StepSimulation(float deltaTime, bool activateAll);

        void TickBodies(float deltaTime);

        JPH::BodyID CreateBody(const JPH::BodyCreationSettings& settings, Objects::Components::PhysicsBody* component, JPH::EActivation activation = JPH::EActivation::Activate);
        void RemoveBody(JPH::BodyID id);

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

        void OnContactAdded(const Body &body1, const Body &body2, const ContactManifold &contactManifold, ContactSettings &contactSettings);
        void OnContactPersisted(const Body &body1, const Body &body2, const ContactManifold &contactManifold, ContactSettings &contactSettings);
        void OnContactRemoved(const SubShapeIDPair &pair);

        std::unordered_map<JPH::BodyID, Objects::Components::PhysicsBody*> bodyIDToComponentMap;

        JPH::TempAllocatorImpl* m_tempAllocator;
        JPH::JobSystem* m_jobSystem;

        JPH::PhysicsSystem m_physicsSystem;

        ObjectLayerPairFilterImpl m_objectLayerFilter;
        ObjectVsBroadPhaseLayerFilterImpl m_objectVsBroadphase;
        BPLayerInterfaceImpl m_broadPhaseLayer;

        PhysicsContactListener m_contactListener;
        PhysicsBodyActivationListener m_activationListener;

        std::queue<std::function<void()>> mBodyUpdateQueue;
        std::mutex mQueueMutex;

        bool initialized = false;

        friend class PhysicsContactListener;
    };

}
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

/// A vector (a size, an offset, a velocity, a force, an impulse) : Jolt keeps these in float, whatever the precision of positions
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

/// Jolt's positions are doubles (JPH_DOUBLE_PRECISION, large worlds) : these keep all of them
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
    inline bool AssertFailedImpl(const char *inExpression, const char *inMessage, const char *inFile, uint inLine)
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

    class PhysicsManager;

    /// Called by Jolt from its jobs (several threads at once, bodies locked) : only records the contacts for its manager, which
    /// hands them to the scripts once the step is over
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
        glm::vec3 point = glm::vec3(0.0f);          ///< where the ray met the body, in the world
        glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);     ///< the surface's normal there, pointing out of the body
    };

    class PhysicsManager {
    public:

        /// @param deterministic  the simulation runs on the calling thread only : Jolt gives the same result on any number of
        /// threads, but the order of the contact callbacks (and so of what the scripts do in them) depends on which job ran first
        void Init(glm::vec3 gravity, bool deterministic = false);
        void Shutdown();

        RaycastResult RayCast(RaycastRequest request);

        /// World units per second squared. Expressed in the project's own units, so a project where
        /// one unit isn't one metre needs this scaled accordingly rather than left at 9.81.
        void SetGravity(glm::vec3 gravity);
        glm::vec3 GetGravity();

        // ---------------- Gravity fields ----------------
        // Jolt has one gravity for the whole system. The fields and the per-body gravity are done on top of it : before every step each
        // active dynamic body gets, as a force, the difference between the gravity it should feel and the one Jolt will give it
        // (see ApplyGravityFields). A project without fields and with every body on the world's gravity pays nothing for it.

        /// Replaces the gravity fields the steps solve : plain numbers, whoever made them (see GravityField::BuildData). Their `order`
        /// breaks the ties between two of the same priority. A sleeping body is woken when the new fields change its gravity.
        void SetGravityFields(std::vector<GravityFieldData> fields);

        /// SetGravityFields with the active gravity fields of the loaded worlds. The engine runs it before every step.
        void CollectGravityFields();

        const std::vector<GravityFieldData>& GetGravityFields() const { return m_GravityFields; }

        /// The gravity a body of the default mode (see GravityMode::Fields) feels at `position` of the root frame : the fields and the
        /// gravity of the world, before any gravity factor
        glm::vec3 GetGravityAt(const glm::dvec3& position);

        /// Where the gravity of a body comes from. The default (GravityMode::Fields) is not kept. Wakes the body up.
        void SetBodyGravity(JPH::BodyID id, GravityMode mode, glm::vec3 custom);

        /// The gravity a body feels now : by its mode, at its center of mass, before its gravity factor
        glm::vec3 GetBodyGravity(JPH::BodyID id);

        /// One step, then the contact callbacks of the scripts (OnContactAdded / Persisted / Ended) for what the step found :
        /// on the calling thread, after Jolt is done, in an order that doesn't depend on which of Jolt's jobs ran first
        void StepSimulation(float deltaTime, bool activateAll);

        /// Fixed steps simulated so far : tells a body whether the pose it reads from Jolt is a new one (see PhysicsBody::GetInterpolationOffset)
        uint64_t GetStepCount() const { return m_StepCount; }

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

        /// A contact reported by Jolt during the step, kept until the step is over. The manifold and settings are copies : what a
        /// script changes in the settings no longer affects the contact (it has already been solved).
        struct PendingContact {
            enum class Kind : uint8_t { Added, Persisted, Removed };
            Kind kind = Kind::Added;
            JPH::BodyID body1, body2;
            JPH::SubShapeID subShape1, subShape2;
            ContactManifold manifold;               // Added / Persisted
            ContactSettings settings;
        };

        /// Adds to every active dynamic body the force that makes the gravity it feels (see GetBodyGravity) what Jolt does not give it
        void ApplyGravityFields();

        /// Wakes the sleeping bodies whose gravity is not the one they fell asleep in, after the fields changed
        void WakeBodiesOfChangedGravity();

        /// The gravity of a body at the position of its center of mass
        glm::vec3 BodyGravityAt(JPH::BodyID id, const glm::dvec3& position, const glm::vec3& world) const;

        // From Jolt's jobs : any thread, under m_ContactMutex
        void QueueContact(PendingContact::Kind kind, const Body &body1, const Body &body2, const ContactManifold &contactManifold, const ContactSettings &contactSettings);
        void QueueContactRemoved(const SubShapeIDPair &pair);

        /// Hands the contacts of the step that just ended to the scripts of both actors, sorted (kind, body ids, sub-shapes)
        void DispatchContacts();
        void DispatchContact(const PendingContact& contact, JPH::BodyID selfID, JPH::BodyID otherID);
        Objects::Components::PhysicsBody* FindComponent(JPH::BodyID id) const;
        /// The scripts of an actor, looked up once per dispatch instead of once per contact
        const std::vector<std::shared_ptr<Objects::Components::Script>>& ScriptsOf(Objects::Actor& actor);

        std::unordered_map<JPH::BodyID, Objects::Components::PhysicsBody*> bodyIDToComponentMap;

        std::vector<GravityFieldData> m_GravityFields;      // sorted (SortGravityFields)
        bool m_GravityFieldsChanged = false;                // since the last step
        std::unordered_map<JPH::BodyID, BodyGravity> m_BodyGravity;         // the bodies that are not on GravityMode::Fields
        std::unordered_map<JPH::BodyID, glm::vec3> m_AppliedGravity;        // the gravity a body felt the last step it was active in
        JPH::BodyIDVector m_ActiveBodies;                   // scratch, kept for its capacity

        std::mutex m_ContactMutex;
        std::vector<PendingContact> m_PendingContacts;          // filled by Jolt's jobs
        std::vector<PendingContact> m_DispatchedContacts;       // the ones being handed out (swapped, so both keep their capacity)
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
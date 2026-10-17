#include "physics_manager.hpp"

#include "engine/platform/thread/thread.hpp"

#include "engine/world/world_manager.hpp"

#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_events.hpp"
#include "engine/physics/physics_world_data.hpp"

#include "engine/world/actor.hpp"

#include "engine/world/engine.hpp"
#include "engine/physics/jolt_job_system.hpp"

#include <algorithm>
#include <tuple>

namespace Shard::Engine::Physics {

    // Constants
    static constexpr uint cMaxBodies = 65536;
    static constexpr uint cNumBodyMutexes = 0;
    static constexpr uint cMaxBodyPairs = 65536;
    static constexpr uint cMaxContactConstraints = 16384;
    static constexpr uint cTempAllocatorSize = 64 * 1024 * 1024;   // scales with the body limits (Jolt sizes its per-step buffers from them)

    void PhysicsManager::Init(glm::vec3 gravity, bool deterministic)
    {
        JPH::RegisterDefaultAllocator();

        JPH::Trace = &TraceImpl;

        JPH_IF_ENABLE_ASSERTS(
            JPH::AssertFailed = [](const char* inExpr, const char* inMsg, const char* inFile, uint inLine)
            {
                std::cerr << inFile << ":" << inLine << ": (" << inExpr << ") " << (inMsg ? inMsg : "") << std::endl;
                return true;
            };
        )

        // Required factory setup
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        // Allocator & job system
        m_tempAllocator = new JPH::TempAllocatorImpl(cTempAllocatorSize);
        // Physics runs on the engine's job system when there is one (it then shares the cores with everything else) ;
        // without an engine (unit tests) it gets a pool of its own
        Core::JobSystem* engineJobs = Core::gSharedEnginePtr ? Core::gSharedEnginePtr->GetJobSystem() : nullptr;
        if(deterministic)
            m_jobSystem = new JPH::JobSystemSingleThreaded(JPH::cMaxPhysicsJobs);
        else if(engineJobs)
            m_jobSystem = new JoltJobSystem(*engineJobs, JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers);
        else
            m_jobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, Core::Platform::HardwareConcurrency() - 1);

        // Initialize physics system
        m_physicsSystem.Init(
            cMaxBodies,
            cNumBodyMutexes,
            cMaxBodyPairs,
            cMaxContactConstraints,
            m_broadPhaseLayer,
            m_objectVsBroadphase,
            m_objectLayerFilter
        );
        

        m_physicsSystem.SetBodyActivationListener(&m_activationListener);
        m_contactListener.owner = this;
        m_physicsSystem.SetContactListener(&m_contactListener);
        m_physicsSystem.SetGravity(JPH::Vec3Arg(gravity.x, gravity.y, gravity.z));

        this->initialized = true;
    }

    void PhysicsManager::Shutdown()
    {
        if (!initialized)
            return;

        bodyIDToComponentMap.clear();
        m_PendingContacts.clear();
        m_DispatchedContacts.clear();
        m_ScriptCache.clear();

        delete m_jobSystem;
        m_jobSystem = nullptr;

        delete m_tempAllocator;
        m_tempAllocator = nullptr;

        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;

        // The job system and allocator this manager's JPH::PhysicsSystem points at are gone, so every
        // accessor has to start failing its guard again - otherwise a call that comes in after
        // shutdown looks valid and dereferences freed memory. Note the manager is not re-initializable
        // afterwards: JPH::PhysicsSystem is held by value and Init() asserts if run on one twice.
        initialized = false;
    }

    void PhysicsManager::SetGravity(glm::vec3 gravity)
    {
        if (!initialized)
            DEBUG_FATAL("Physics system is not yet initialized");

        m_physicsSystem.SetGravity(JPH::Vec3Arg(gravity.x, gravity.y, gravity.z));

        // Bodies sleeping under the old gravity would otherwise stay put until something else wakes
        // them, which looks like the new value hasn't been applied at all.
        JPH::BodyIDVector bodies;
        m_physicsSystem.GetBodies(bodies);
        m_physicsSystem.GetBodyInterface().ActivateBodies(bodies.data(), bodies.size());
    }

    glm::vec3 PhysicsManager::GetGravity()
    {
        if (!initialized)
            DEBUG_FATAL("Physics system is not yet initialized");

        JPH::Vec3 gravity = m_physicsSystem.GetGravity();
        return glm::vec3(gravity.GetX(), gravity.GetY(), gravity.GetZ());
    }

    RaycastResult PhysicsManager::RayCast(RaycastRequest request)
    {
        if (!initialized)
            DEBUG_FATAL("Physics system is not yet initialized");

        RaycastResult result = {};

        request.direction *= request.maxDistance;

        JPH::RVec3 origin(request.origin.x, request.origin.y, request.origin.z);
        JPH::RVec3 direction(request.direction.x, request.direction.y, request.direction.z);

        JPH::RRayCast ray(origin, direction);

        RaycastBroadPhaseFilter broadPhaseFilter(request.broadPhaseMask);
        RaycastObjectLayerFilter objectLayerFilter(request.objectMask);

        RayCastResult ioHit;

        BodyFilter defaultBodyFilter;

        const BodyFilter& bodyFilter =
            request.bodyFilter ? *request.bodyFilter : defaultBodyFilter;

        if(m_physicsSystem.GetNarrowPhaseQuery().CastRay(ray, ioHit, broadPhaseFilter, objectLayerFilter, bodyFilter)){
            result.hit = true;
            result.hitBody = bodyIDToComponentMap.at(ioHit.mBodyID);
            result.hitDistance = ioHit.mFraction * request.maxDistance;
        }

        return result;
    }

    void PhysicsManager::QueueContact(PendingContact::Kind kind, const Body &body1, const Body &body2, const ContactManifold &contactManifold, const ContactSettings &contactSettings)
    {
        // Jolt's jobs report contacts concurrently, with the bodies locked : nothing here may run game code, lock a body or touch
        // the component map (the main thread is waiting in Update, the scripts run once it returns)
        std::lock_guard<std::mutex> lock(m_ContactMutex);
        PendingContact& contact = m_PendingContacts.emplace_back();
        contact.kind = kind;
        contact.body1 = body1.GetID();
        contact.body2 = body2.GetID();
        contact.subShape1 = contactManifold.mSubShapeID1;
        contact.subShape2 = contactManifold.mSubShapeID2;
        contact.manifold = contactManifold;
        contact.settings = contactSettings;
    }

    void PhysicsManager::QueueContactRemoved(const SubShapeIDPair &pair)
    {
        std::lock_guard<std::mutex> lock(m_ContactMutex);
        PendingContact& contact = m_PendingContacts.emplace_back();
        contact.kind = PendingContact::Kind::Removed;
        contact.body1 = pair.GetBody1ID();
        contact.body2 = pair.GetBody2ID();
        contact.subShape1 = pair.GetSubShapeID1();
        contact.subShape2 = pair.GetSubShapeID2();
    }

    Objects::Components::PhysicsBody* PhysicsManager::FindComponent(JPH::BodyID id) const
    {
        auto it = bodyIDToComponentMap.find(id);
        return it != bodyIDToComponentMap.end() ? it->second : nullptr;
    }

    const std::vector<std::shared_ptr<Objects::Components::Script>>& PhysicsManager::ScriptsOf(Objects::Actor& actor)
    {
        auto it = m_ScriptCache.find(&actor);
        if (it == m_ScriptCache.end())
            it = m_ScriptCache.emplace(&actor, actor.GetComponents<Objects::Components::Script>()).first;
        return it->second;
    }

    void PhysicsManager::DispatchContacts()
    {
        {
            std::lock_guard<std::mutex> lock(m_ContactMutex);
            m_DispatchedContacts.swap(m_PendingContacts);
        }
        if (m_DispatchedContacts.empty())
            return;

        // Jolt's jobs queued them in whatever order they happened to finish : sorted, so the scripts see the same sequence with
        // one thread or twenty (and a replay stays a replay). Kind first : the step's new contacts, then the lasting ones, then
        // the ended ones.
        auto key = [](const PendingContact& c) {
            return std::make_tuple(static_cast<int>(c.kind), c.body1.GetIndexAndSequenceNumber(), c.body2.GetIndexAndSequenceNumber(),
                                   c.subShape1.GetValue(), c.subShape2.GetValue());
        };
        m_DispatchOrder.resize(m_DispatchedContacts.size());
        for (uint32_t i = 0; i < m_DispatchOrder.size(); i++)
            m_DispatchOrder[i] = i;
        std::stable_sort(m_DispatchOrder.begin(), m_DispatchOrder.end(), [&](uint32_t a, uint32_t b) {
            return key(m_DispatchedContacts[a]) < key(m_DispatchedContacts[b]);
        });

        for (uint32_t i : m_DispatchOrder)
        {
            const PendingContact& contact = m_DispatchedContacts[i];
            DispatchContact(contact, contact.body1, contact.body2);
            DispatchContact(contact, contact.body2, contact.body1);
        }

        m_DispatchedContacts.clear();
        m_ScriptCache.clear();
    }

    void PhysicsManager::DispatchContact(const PendingContact& contact, JPH::BodyID selfID, JPH::BodyID otherID)
    {
        Objects::Components::PhysicsBody* self = FindComponent(selfID);
        Objects::Components::PhysicsBody* other = FindComponent(otherID);
        if (!self || !other || !self->parent || !other->parent)
            return;

        // Held for the duration of the calls : a script may destroy either actor
        std::shared_ptr<Objects::Actor> selfActor = self->parent;
        std::shared_ptr<Objects::Actor> otherActor = other->parent;

        // The event's own copy : a script may write to it, the contact was already solved so it changes nothing
        ContactSettings settings = contact.settings;

        for (const auto& script : ScriptsOf(*selfActor))
        {
            // A previous callback (of this contact or an earlier one) removed one of the two bodies : the contact went with it
            if (FindComponent(selfID) != self || FindComponent(otherID) != other)
                return;

            switch (contact.kind)
            {
                case PendingContact::Kind::Added:
                    script->OnContactAdded(Events::ContactAddedEvent(*other, contact.manifold, settings, Core::ObjectID(0)));
                    break;
                case PendingContact::Kind::Persisted:
                    script->OnContactPersisted(Events::ContactPersistedEvent(*other, contact.manifold, settings, Core::ObjectID(0)));
                    break;
                case PendingContact::Kind::Removed:
                    script->OnContactEnded(Events::ContactRemovedEvent(*other, Core::ObjectID(0)));
                    break;
            }
        }
    }

    void PhysicsManager::StepSimulation(float deltaTime, bool activateAll)
    {
        if(activateAll){
            JPH::BodyIDVector bodies;
            m_physicsSystem.GetBodies(bodies);
            m_physicsSystem.GetBodyInterface().ActivateBodies(bodies.data(), bodies.size());
        }

        // Many bodies added since the last step : rebuild the broadphase tree once, rather than query a degraded one
        if (m_bodiesAddedSinceOptimize >= 32)
        {
            m_physicsSystem.OptimizeBroadPhase();
            m_bodiesAddedSinceOptimize = 0;
        }

        m_physicsSystem.Update(deltaTime, 1, m_tempAllocator, m_jobSystem);
        m_StepCount++;

        // Jolt is done with the bodies : the scripts may now read them, move them, destroy their actors
        DispatchContacts();
    }
 
    void PhysicsManager::TickBodies(float deltaTime){
        
        Worlds::WorldManager* worlds = Core::GetEngine().GetWorldManager();
        const int worldCount = worlds->GetLoadedWorldCount();
        for(int i = 0; i < worldCount; i++){
            Worlds::World* world = worlds->GetWorldAt(i);
            if(!world)
                continue;
            for (auto& [id,physicsBody] : world->Ext<PhysicsWorldData>().bodies){
                if(physicsBody->Active())
                    physicsBody->Tick(deltaTime);
            }
        }
    }

    JPH::BodyID PhysicsManager::CreateBody(const JPH::BodyCreationSettings &settings, Objects::Components::PhysicsBody *component, JPH::EActivation activation)
    {
        JPH::BodyInterface& bodyInterface = m_physicsSystem.GetBodyInterface();

        // Create body
        JPH::Body* body = bodyInterface.CreateBody(settings);
        if (body == nullptr)
        {
            DEBUG_ERROR("Failed to create body!");
            return JPH::BodyID();
        }

        // Add to worlds
        bodyInterface.AddBody(body->GetID(), activation);
        bodyIDToComponentMap[body->GetID()] = component;
        m_bodiesAddedSinceOptimize++;
        return body->GetID();
    }

    void PhysicsManager::RemoveBody(JPH::BodyID id)
    {
        JPH::BodyInterface& bodyInterface = m_physicsSystem.GetBodyInterface();
        bodyInterface.RemoveBody(id);
        bodyInterface.DestroyBody(id);
        bodyIDToComponentMap.erase(id);
    }

    ValidateResult PhysicsContactListener::OnContactValidate(const Body &inBody1, const Body &inBody2, RVec3Arg inBaseOffset, const CollideShapeResult &inCollisionResult)
    {
        return ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void PhysicsContactListener::OnContactAdded(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings)
    {
        owner->QueueContact(PhysicsManager::PendingContact::Kind::Added, inBody1, inBody2, inManifold, ioSettings);
    }

    void PhysicsContactListener::OnContactPersisted(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings)
    {
        owner->QueueContact(PhysicsManager::PendingContact::Kind::Persisted, inBody1, inBody2, inManifold, ioSettings);
    }

    void PhysicsContactListener::OnContactRemoved(const SubShapeIDPair &inSubShapePair)
    {
        owner->QueueContactRemoved(inSubShapePair);
    }
}
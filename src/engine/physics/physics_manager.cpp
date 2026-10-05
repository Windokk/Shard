#include "physics_manager.hpp"

#include "engine/world/levels/level_manager.hpp"

#include "engine/physics/physics_body.hpp"

#include "engine/world/actor.hpp"

#include "engine/core/engine.hpp"

namespace Shard::Engine::Physics {

    // Constants
    static constexpr uint cMaxBodies = 1024;
    static constexpr uint cNumBodyMutexes = 0;
    static constexpr uint cMaxBodyPairs = 1024;
    static constexpr uint cMaxContactConstraints = 1024;
    static constexpr uint cTempAllocatorSize = 10 * 1024 * 1024;

    void PhysicsManager::Init(glm::vec3 gravity)
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
        m_jobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

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
        m_physicsSystem.SetContactListener(&m_contactListener);
        m_physicsSystem.SetGravity(JPH::Vec3Arg(gravity.x, gravity.y, gravity.z));

        this->initialized = true;
    }

    void PhysicsManager::Shutdown()
    {
        if (!initialized)
            return;

        bodyIDToComponentMap.clear();

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

    void PhysicsManager::OnContactAdded(const Body &body1, const Body &body2, const ContactManifold &contactManifold, ContactSettings &contactSettings)
    {   
        Objects::Components::PhysicsBody* comp1 = bodyIDToComponentMap[body1.GetID()];
        Objects::Components::PhysicsBody* comp2 = bodyIDToComponentMap[body2.GetID()];

        if(!comp1 || !comp2)
            return;

        for(auto& script : comp1->parent->GetComponents<Objects::Components::Script>()){
            Core::GetEngine().GetEventDispatcher()->emitToComponent(
                script->parent->GetComponentIDInLevel(script->GetLocalId()),
                Events::ContactAddedEvent(*comp2, contactManifold, contactSettings, Core::ObjectID(0))
            );
        }

        for(auto& script : comp2->parent->GetComponents<Objects::Components::Script>()){
            Core::GetEngine().GetEventDispatcher()->emitToComponent(
                script->parent->GetComponentIDInLevel(script->GetLocalId()),
                Events::ContactAddedEvent(*comp1, contactManifold, contactSettings, Core::ObjectID(0))
            );
        }
    }

    void PhysicsManager::OnContactPersisted(const Body &body1, const Body &body2, const ContactManifold &contactManifold, ContactSettings &contactSettings)
    {
        Objects::Components::PhysicsBody* comp1 = bodyIDToComponentMap[body1.GetID()];
        Objects::Components::PhysicsBody* comp2 = bodyIDToComponentMap[body2.GetID()];

        if(!comp1 || !comp2)
            return;

        for(auto& script : comp1->parent->GetComponents<Objects::Components::Script>()){
            Core::GetEngine().GetEventDispatcher()->emitToComponent(
                script->parent->GetComponentIDInLevel(script->GetLocalId()),
                Events::ContactPersistedEvent(*comp2, contactManifold, contactSettings, Core::ObjectID(0))
            );
        }

        for(auto& script : comp2->parent->GetComponents<Objects::Components::Script>()){
            Core::GetEngine().GetEventDispatcher()->emitToComponent(
                script->parent->GetComponentIDInLevel(script->GetLocalId()),
                Events::ContactPersistedEvent(*comp1, contactManifold, contactSettings, Core::ObjectID(0))
            );
        }
    }
    
    void PhysicsManager::OnContactRemoved(const SubShapeIDPair &pair)
    {
        Objects::Components::PhysicsBody* comp1 = bodyIDToComponentMap[pair.GetBody1ID()];
        Objects::Components::PhysicsBody* comp2 = bodyIDToComponentMap[pair.GetBody2ID()];

        if(!comp1 || !comp2)
            return;

        for(auto& script : comp1->parent->GetComponents<Objects::Components::Script>()){
            Core::GetEngine().GetEventDispatcher()->emitToComponent(
                script->parent->GetComponentIDInLevel(script->GetLocalId()),
                Events::ContactRemovedEvent(*comp2, Core::ObjectID(0))
            );
        }

        for(auto& script : comp2->parent->GetComponents<Objects::Components::Script>()){
            Core::GetEngine().GetEventDispatcher()->emitToComponent(
                script->parent->GetComponentIDInLevel(script->GetLocalId()),
                Events::ContactRemovedEvent(*comp1, Core::ObjectID(0))
            );
        }

    }

    void PhysicsManager::StepSimulation(float deltaTime, bool activateAll)
    {
        if(activateAll){
            JPH::BodyIDVector bodies;
            m_physicsSystem.GetBodies(bodies);
            m_physicsSystem.GetBodyInterface().ActivateBodies(bodies.data(), bodies.size());
        }

        m_physicsSystem.Update(deltaTime, 1, m_tempAllocator, m_jobSystem);
    }
 
    void PhysicsManager::TickBodies(float deltaTime){
        
        if(int levelCount = Core::GetEngine().GetLevelManager()->GetLoadedLevelCount() > 0){
            for(int i = 0; i < levelCount; i++){
                for (auto& [id,physicsBody] : Core::GetEngine().GetLevelManager()->GetLevelAt(i)->physicsBodies){
                    if(physicsBody->Active())
                        physicsBody->Tick(deltaTime);
                }
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
        Core::GetEngine().GetPhysicsManager()->OnContactAdded(inBody1, inBody2, inManifold, ioSettings);
    }

    void PhysicsContactListener::OnContactPersisted(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings)
    {
        Core::GetEngine().GetPhysicsManager()->OnContactPersisted(inBody1, inBody2, inManifold, ioSettings);
    }

    void PhysicsContactListener::OnContactRemoved(const SubShapeIDPair &inSubShapePair)
    {
        Core::GetEngine().GetPhysicsManager()->OnContactRemoved(inSubShapePair);
    }
}
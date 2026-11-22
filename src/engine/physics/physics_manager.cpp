#include "physics_manager.hpp"

#include "engine/platform/thread/thread.hpp"

#include "engine/world/world_manager.hpp"

#include "engine/physics/constraints/physics_constraint.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_events.hpp"
#include "engine/physics/physics_world_data.hpp"
#include "engine/physics/gravity_field.hpp"

#include <Jolt/Physics/Body/BodyLock.h>

#include "engine/world/actor.hpp"

#include "engine/world/engine.hpp"
#include "engine/physics/jolt_job_system.hpp"

#include <algorithm>
#include <tuple>

namespace Shard::Engine::Physics {

    static constexpr uint cMaxBodies = 65536;
    static constexpr uint cNumBodyMutexes = 0;
    static constexpr uint cMaxBodyPairs = 65536;
    static constexpr uint cMaxContactConstraints = 16384;
    static constexpr uint cTempAllocatorSize = 64 * 1024 * 1024;

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

        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        m_tempAllocator = new JPH::TempAllocatorImpl(cTempAllocatorSize);
        Core::JobSystem* engineJobs = Core::gSharedEnginePtr ? Core::gSharedEnginePtr->GetJobSystem() : nullptr;
        if(deterministic)
            m_jobSystem = new JPH::JobSystemSingleThreaded(JPH::cMaxPhysicsJobs);
        else if(engineJobs)
            m_jobSystem = new JoltJobSystem(*engineJobs, JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers);
        else
            m_jobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, Core::Platform::HardwareConcurrency() - 1);

        m_Collision.Init(this);

        m_physicsSystem.Init(
            cMaxBodies,
            cNumBodyMutexes,
            cMaxBodyPairs,
            cMaxContactConstraints,
            m_Collision.BroadPhaseInterface(),
            m_Collision.ObjectVsBroadPhase(),
            m_Collision.PairFilter()
        );

        m_physicsSystem.SetCombineFriction([](const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b, const JPH::SubShapeID&) {
            return CollisionSystem::Active()->CombineFriction(a, b);
        });
        m_physicsSystem.SetCombineRestitution([](const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b, const JPH::SubShapeID&) {
            return CollisionSystem::Active()->CombineRestitution(a, b);
        });

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

        ClearConstraints();
        m_MeshColliders.Clear();
        m_Collision.Shutdown();
        bodyIDToComponentMap.clear();
        m_PendingContacts.clear();
        m_DispatchedContacts.clear();
        m_ScriptCache.clear();
        m_GravityFields.clear();
        m_BodyGravity.clear();
        m_AppliedGravity.clear();
        m_ActiveBodies.clear();

        delete m_jobSystem;
        m_jobSystem = nullptr;

        delete m_tempAllocator;
        m_tempAllocator = nullptr;

        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;

        // Guards must fail again after shutdown. Not re-initializable: JPH::PhysicsSystem is held by value and Init() asserts if run twice.
        initialized = false;
    }

    void PhysicsManager::SetGravity(glm::vec3 gravity)
    {
        if (!initialized)
            DEBUG_FATAL("Physics system is not yet initialized");

        m_physicsSystem.SetGravity(JPH::Vec3Arg(gravity.x, gravity.y, gravity.z));

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

    void PhysicsManager::SetGravityFields(std::vector<GravityFieldData> fields)
    {
        SortGravityFields(fields);

        if (fields == m_GravityFields)
            return;

        m_GravityFields = std::move(fields);
        m_GravityFieldsChanged = true;
    }

    void PhysicsManager::CollectGravityFields()
    {
        std::vector<GravityFieldData> fields;

        Worlds::WorldManager* worlds = Core::GetEngine().GetWorldManager();
        const int worldCount = worlds ? worlds->GetLoadedWorldCount() : 0;
        uint32_t order = 0;
        for (int i = 0; i < worldCount; i++)
        {
            Worlds::World* world = worlds->GetWorldAt(i);
            if (!world)
                continue;

            for (auto& [id, field] : world->Ext<PhysicsWorldData>().gravityFields)
            {
                if (!field || !field->Active())
                    continue;

                GravityFieldData data = field->BuildData();
                data.order = order++;
                fields.push_back(data);
            }
        }

        SetGravityFields(std::move(fields));
    }

    glm::vec3 PhysicsManager::GetGravityAt(const glm::dvec3& position)
    {
        return SampleGravityFields(m_GravityFields, position, GetGravity());
    }

    void PhysicsManager::SetBodyGravity(JPH::BodyID id, GravityMode mode, glm::vec3 custom)
    {
        if (id.IsInvalid())
            return;

        if (mode == GravityMode::Fields)
            m_BodyGravity.erase(id);
        else
            m_BodyGravity[id] = BodyGravity{ mode, custom };

        m_AppliedGravity.erase(id);
    }

    glm::vec3 PhysicsManager::BodyGravityAt(JPH::BodyID id, const glm::dvec3& position, const glm::vec3& world) const
    {
        auto it = m_BodyGravity.find(id);
        return ResolveBodyGravity(m_GravityFields, it != m_BodyGravity.end() ? it->second : BodyGravity{}, position, world);
    }

    glm::vec3 PhysicsManager::GetBodyGravity(JPH::BodyID id)
    {
        const glm::vec3 world = GetGravity();

        JPH::BodyLockRead lock(m_physicsSystem.GetBodyLockInterface(), id);
        if (!lock.Succeeded())
            return world;

        return BodyGravityAt(id, ToGLMD(lock.GetBody().GetCenterOfMassPosition()), world);
    }

    void PhysicsManager::WakeBodiesOfChangedGravity()
    {
        const glm::vec3 world = GetGravity();

        std::vector<JPH::BodyID> toWake;
        for (const auto& [id, component] : bodyIDToComponentMap)
        {
            if (m_BodyGravity.find(id) != m_BodyGravity.end() || (component && !component->Active()))
                continue;

            JPH::BodyLockRead lock(m_physicsSystem.GetBodyLockInterface(), id);
            if (!lock.Succeeded())
                continue;

            const JPH::Body& body = lock.GetBody();
            if (!body.IsDynamic() || body.IsActive())
                continue;

            const glm::vec3 now = SampleGravityFields(m_GravityFields, ToGLMD(body.GetCenterOfMassPosition()), world);
            auto it = m_AppliedGravity.find(id);
            const glm::vec3 before = it != m_AppliedGravity.end() ? it->second : world;

            if (glm::length(now - before) > 1e-3f + 1e-4f * glm::length(before))
                toWake.push_back(id);
        }

        if (toWake.empty())
            return;

        // Wake in a fixed order: it is the order bodies are simulated in.
        std::sort(toWake.begin(), toWake.end(), [](JPH::BodyID a, JPH::BodyID b) {
            return a.GetIndexAndSequenceNumber() < b.GetIndexAndSequenceNumber();
        });
        m_physicsSystem.GetBodyInterface().ActivateBodies(toWake.data(), (int)toWake.size());
    }

    void PhysicsManager::ApplyGravityFields()
    {
        // Forces are consumed by the one collision step of the next Update. With several collision steps the first would take them all.

        if (m_GravityFieldsChanged)
        {
            WakeBodiesOfChangedGravity();
            m_GravityFieldsChanged = false;
        }

        if (m_GravityFields.empty() && m_BodyGravity.empty())
        {
            m_AppliedGravity.clear();
            return;
        }

        const glm::vec3 world = GetGravity();

        auto apply = [&](JPH::BodyID id) {
            JPH::BodyLockWrite lock(m_physicsSystem.GetBodyLockInterface(), id);
            if (!lock.Succeeded())
                return;

            JPH::Body& body = lock.GetBody();
            if (!body.IsDynamic() || !body.IsActive())
                return;

            JPH::MotionProperties* motion = body.GetMotionProperties();
            const float inverseMass = motion->GetInverseMassUnchecked();
            if (inverseMass <= 0.0f)
                return;

            const glm::vec3 gravity = BodyGravityAt(id, ToGLMD(body.GetCenterOfMassPosition()), world);
            if (m_BodyGravity.find(id) == m_BodyGravity.end())
                m_AppliedGravity[id] = gravity;

            // Jolt already adds `gravityFactor * world`, so apply the difference as a force (a = F / m).
            const glm::vec3 missing = gravity - world;
            if (glm::dot(missing, missing) <= 1e-12f)
                return;

            body.AddForce(ToJolt(missing * (motion->GetGravityFactor() / inverseMass)));
        };

        if (m_GravityFields.empty())
        {
            std::vector<JPH::BodyID> ids;
            ids.reserve(m_BodyGravity.size());
            for (const auto& [id, gravity] : m_BodyGravity)
                ids.push_back(id);
            for (JPH::BodyID id : ids)
                apply(id);
            return;
        }

        m_physicsSystem.GetActiveBodies(JPH::EBodyType::RigidBody, m_ActiveBodies);
        for (JPH::BodyID id : m_ActiveBodies)
            apply(id);
    }

    RaycastResult PhysicsManager::RayCast(RaycastRequest request)
    {
        if (!initialized)
            DEBUG_FATAL("Physics system is not yet initialized");

        RaycastResult result = {};

        request.direction *= request.maxDistance;

        JPH::RVec3 origin(request.origin.x, request.origin.y, request.origin.z);
        JPH::Vec3 direction(request.direction.x, request.direction.y, request.direction.z);

        JPH::RRayCast ray(origin, direction);

        RaycastBroadPhaseFilter broadPhaseFilter(request.broadPhaseMask);
        RaycastObjectLayerFilter objectLayerFilter(m_Collision.Classes(), request.objectMask);

        RayCastResult ioHit;

        BodyFilter defaultBodyFilter;

        const BodyFilter& bodyFilter =
            request.bodyFilter ? *request.bodyFilter : defaultBodyFilter;

        if(m_physicsSystem.GetNarrowPhaseQuery().CastRay(ray, ioHit, broadPhaseFilter, objectLayerFilter, bodyFilter)){
            result.hit = true;
            result.hitBody = bodyIDToComponentMap.at(ioHit.mBodyID);
            result.hitDistance = ioHit.mFraction * request.maxDistance;

            const JPH::RVec3 point = ray.GetPointOnRay(ioHit.mFraction);
            result.point = glm::vec3(float(point.GetX()), float(point.GetY()), float(point.GetZ()));
            JPH::BodyLockRead lock(m_physicsSystem.GetBodyLockInterface(), ioHit.mBodyID);
            if (lock.Succeeded()) {
                const JPH::Vec3 normal = lock.GetBody().GetWorldSpaceSurfaceNormal(ioHit.mSubShapeID2, point);
                result.normal = glm::vec3(normal.GetX(), normal.GetY(), normal.GetZ());
                result.onMesh = MeshSubmeshAt(*lock.GetBody().GetShape(), ioHit.mSubShapeID2, result.submesh);
            }
        }

        return result;
    }

    void PhysicsManager::QueueContact(PendingContact::Kind kind, const Body &body1, const Body &body2, const ContactManifold &contactManifold, const ContactSettings &contactSettings)
    {
        // Called from Jolt jobs with bodies locked: no game code, no body locks, no component map access.
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

        // Jolt jobs finish in any order. Sort so scripts see the same sequence on any thread count. New contacts first, then lasting, then ended.
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

        std::shared_ptr<Objects::Actor> selfActor = self->parent;
        std::shared_ptr<Objects::Actor> otherActor = other->parent;

        ContactSettings settings = contact.settings;

        for (const auto& script : ScriptsOf(*selfActor))
        {
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

        if (m_bodiesAddedSinceOptimize >= 32)
        {
            m_physicsSystem.OptimizeBroadPhase();
            m_bodiesAddedSinceOptimize = 0;
        }

        ApplyGravityFields();
        m_Collision.BeginStep();

        m_physicsSystem.Update(deltaTime, 1, m_tempAllocator, m_jobSystem);
        m_StepCount++;

        DispatchContacts();
        m_Collision.EndStep(m_StepCount);
        CheckConstraintBreaks(deltaTime);
    }
 
    void PhysicsManager::TickBodies(float deltaTime){
        m_MeshColliders.Poll();
        
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

        TickConstraints(Core::GetEngine().IsInPlayMode());
    }

    void PhysicsManager::TickConstraints(bool playing)
    {
        if(!playing)
        {
            ClearConstraints();
            return;
        }

        Worlds::WorldManager* worlds = Core::GetEngine().GetWorldManager();
        const int worldCount = worlds->GetLoadedWorldCount();
        for(int i = 0; i < worldCount; i++){
            Worlds::World* world = worlds->GetWorldAt(i);
            if(!world)
                continue;
            const auto constraints = world->Ext<PhysicsWorldData>().constraints;
            for (auto& [id, constraint] : constraints)
                constraint->Sync(*this);
        }
    }

    void PhysicsManager::RegisterConstraint(Objects::Components::PhysicsConstraint* owner, JPH::BodyID a, JPH::BodyID b, JPH::TwoBodyConstraint* constraint)
    {
        ConstraintRecord record;
        record.owner = owner;
        record.a = a;
        record.b = b;
        record.constraint = constraint;
        m_physicsSystem.AddConstraint(constraint);
        m_Constraints.push_back(std::move(record));
        WakeConstraintBodies(a, b);
    }

    void PhysicsManager::UnregisterConstraint(Objects::Components::PhysicsConstraint* owner)
    {
        for (auto it = m_Constraints.begin(); it != m_Constraints.end(); ++it)
        {
            if (it->owner != owner)
                continue;
            m_physicsSystem.RemoveConstraint(it->constraint);
            WakeConstraintBodies(it->a, it->b);
            m_Constraints.erase(it);
            return;
        }
    }

    void PhysicsManager::ClearConstraints()
    {
        while (!m_Constraints.empty())
        {
            Objects::Components::PhysicsConstraint* owner = m_Constraints.back().owner;
            UnregisterConstraint(owner);
            owner->OnConstraintDropped();
        }
    }

    void PhysicsManager::DropConstraintsOf(JPH::BodyID id)
    {
        std::vector<Objects::Components::PhysicsConstraint*> owners;
        for (const ConstraintRecord& record : m_Constraints)
            if (record.a == id || record.b == id)
                owners.push_back(record.owner);

        for (Objects::Components::PhysicsConstraint* owner : owners)
        {
            UnregisterConstraint(owner);
            owner->OnConstraintDropped();
        }
    }

    void PhysicsManager::CheckConstraintBreaks(float deltaTime)
    {
        if (m_Constraints.empty())
            return;

        std::vector<Objects::Components::PhysicsConstraint*> owners;
        owners.reserve(m_Constraints.size());
        for (const ConstraintRecord& record : m_Constraints)
            owners.push_back(record.owner);

        for (Objects::Components::PhysicsConstraint* owner : owners)
            owner->CheckBreak(deltaTime);
    }

    void PhysicsManager::WakeConstraintBodies(JPH::BodyID a, JPH::BodyID b)
    {
        JPH::BodyInterface& bodies = m_physicsSystem.GetBodyInterface();
        for (JPH::BodyID id : { a, b })
            if (!id.IsInvalid() && bodies.GetMotionType(id) != JPH::EMotionType::Static)
                bodies.ActivateBody(id);
    }

    JPH::BodyID PhysicsManager::CreateBody(const JPH::BodyCreationSettings &settings, Objects::Components::PhysicsBody *component, JPH::EActivation activation)
    {
        JPH::BodyInterface& bodyInterface = m_physicsSystem.GetBodyInterface();

        JPH::Body* body = bodyInterface.CreateBody(settings);
        if (body == nullptr)
        {
            DEBUG_ERROR("Failed to create body!");
            return JPH::BodyID();
        }

        bodyInterface.AddBody(body->GetID(), activation);
        bodyIDToComponentMap[body->GetID()] = component;
        m_bodiesAddedSinceOptimize++;
        return body->GetID();
    }

    JPH::BodyID PhysicsManager::CreateBody(const JPH::BodyCreationSettings &settings, Objects::Components::PhysicsBody *component, ColliderState&& collider, JPH::EActivation activation)
    {
        const JPH::BodyID id = CreateBody(settings, component, activation);
        if (id.IsInvalid())
            m_Collision.Discard(collider);
        else
            m_Collision.Attach(id, std::move(collider), component);
        return id;
    }

    void PhysicsManager::RemoveBody(JPH::BodyID id, Collision::EndCause cause)
    {
        DropConstraintsOf(id);
        m_Collision.Detach(id, cause);
        JPH::BodyInterface& bodyInterface = m_physicsSystem.GetBodyInterface();
        bodyInterface.RemoveBody(id);
        bodyInterface.DestroyBody(id);
        bodyIDToComponentMap.erase(id);
        m_BodyGravity.erase(id);
        m_AppliedGravity.erase(id);
    }

    ValidateResult PhysicsContactListener::OnContactValidate(const Body &inBody1, const Body &inBody2, RVec3Arg inBaseOffset, const CollideShapeResult &inCollisionResult)
    {
        return owner->Collision().OnValidate(inBody1, inBody2);
    }

    void PhysicsContactListener::OnContactAdded(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings)
    {
        owner->Collision().OnContact(true, inBody1, inBody2, inManifold, ioSettings);
        owner->QueueContact(PhysicsManager::PendingContact::Kind::Added, inBody1, inBody2, inManifold, ioSettings);
    }

    void PhysicsContactListener::OnContactPersisted(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings)
    {
        owner->Collision().OnContact(false, inBody1, inBody2, inManifold, ioSettings);
        owner->QueueContact(PhysicsManager::PendingContact::Kind::Persisted, inBody1, inBody2, inManifold, ioSettings);
    }

    void PhysicsContactListener::OnContactRemoved(const SubShapeIDPair &inSubShapePair)
    {
        owner->Collision().OnContactRemoved(inSubShapePair);
        owner->QueueContactRemoved(inSubShapePair);
    }
}
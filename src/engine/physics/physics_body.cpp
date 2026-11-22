#include "physics_body.hpp"

#include <cctype>

#include "engine/assets/serialization/reflection/reflection_serializer.hpp"

#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"

#include "physics_body.reflection.hpp"

#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Body/BodyLock.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <iostream>
#include <cstring>

#include <thread>

namespace Shard::Engine::Objects::Components{

    namespace {
        void InitShapeParams(InstancedStruct& params, Physics::PhysicsShape type)
        {
            switch (type)
            {
                case Physics::PhysicsShape::BOX:      params.Initialize(&BoxParams_descriptor); break;
                case Physics::PhysicsShape::SPHERE:   params.Initialize(&SphereParams_descriptor); break;
                case Physics::PhysicsShape::CAPSULE:  params.Initialize(&CapsuleParams_descriptor); break;
                case Physics::PhysicsShape::CYLINDER: params.Initialize(&CylinderParams_descriptor); break;
                case Physics::PhysicsShape::MESH:     params.Initialize(&MeshParams_descriptor); break;
            }
        }

        bool ParseShapeTypeString(const std::string& s, Physics::PhysicsShape& out)
        {
            if (s == "box") { out = Physics::PhysicsShape::BOX; return true; }
            if (s == "sphere") { out = Physics::PhysicsShape::SPHERE; return true; }
            if (s == "capsule") { out = Physics::PhysicsShape::CAPSULE; return true; }
            if (s == "cylinder") { out = Physics::PhysicsShape::CYLINDER; return true; }
            if (s == "mesh") { out = Physics::PhysicsShape::MESH; return true; }
            return false;
        }

        const char* ShapeTypeToString(Physics::PhysicsShape shape)
        {
            switch (shape)
            {
                case Physics::BOX:      return "box";
                case Physics::SPHERE:   return "sphere";
                case Physics::CAPSULE:  return "capsule";
                case Physics::CYLINDER: return "cylinder";
                case Physics::MESH:     return "mesh";
            }
            return "box";
        }

        bool ParseShapeParams(const json& p, Physics::PhysicsShape shape, InstancedStruct& outParams, const std::string& parentName, const Serialization::ReflectionContext& context)
        {
            InitShapeParams(outParams, shape);
            if (!outParams.descriptor)
            {
                DEBUG_ERROR("Unhandled physics shape for actor: " + parentName);
                return false;
            }

            json fields = p;

            if (shape == Physics::PhysicsShape::BOX && p.contains("x") && !p.contains("halfExtent"))
                fields = json{ { "halfExtent", p } };

            Serialization::ReadFields(outParams.descriptor->fields, outParams.data, fields, context);
            return true;
        }

        void SerializeShapeParams(ordered_json& out, const InstancedStruct& params, const Serialization::ReflectionContext& context)
        {
            if (params.descriptor)
                Serialization::WriteFields(params.descriptor->fields, params.data, out, context);
        }
    }

    Serialization::ReflectionContext PhysicsBody::ContextFor(Physics::PhysicsShape shape) const
    {
        if (shape != Physics::PhysicsShape::MESH || !GetEngineContext())
            return Serialization::ReflectionContext{};
        return Serialization::ReflectionContext{ GetEngineContext()->GetAssetIDManager() };
    }

    PhysicsBody::PhysicsBody(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {
        shapes.emplace_back();
        shapes[0].params.Initialize(&BoxParams_descriptor);
    }

    JPH::ShapeRefC PhysicsBody::CreateJoltShape(Physics::PhysicsShape shape, const InstancedStruct& params, EMotionType motion)
    {
        // Jolt shapes have no scale: bake the composed world scale in at creation, otherwise a shape under a scaled actor is the wrong size.
        glm::vec3 scale = parent->transform->GetWorldScale();

        JPH::ShapeRefC result;

        switch (shape)
        {
            case Physics::PhysicsShape::SPHERE:
            {
                auto p = reinterpret_cast<const SphereParams*>(params.data);
                if (!p) return nullptr;

                float size = std::max({ scale.x, scale.y, scale.z });

                JPH::SphereShapeSettings s(p->radius * size);

                auto r = s.Create();
                if (r.HasError()) { return nullptr; }
                result = r.Get();
                break;
            }

            case Physics::PhysicsShape::BOX:
            {
                auto p = reinterpret_cast<const BoxParams*>(params.data);
                if (!p) return nullptr;

                JPH::BoxShapeSettings s(
                    JPH::Vec3(p->halfExtent.x * scale.x, p->halfExtent.y * scale.y, p->halfExtent.z * scale.z)
                );

                auto r = s.Create();
                if (r.HasError()) { return nullptr; }
                result = r.Get();
                break;
            }

            case Physics::PhysicsShape::CAPSULE:
            {
                auto p = reinterpret_cast<const CapsuleParams*>(params.data);
                if (!p) return nullptr;

                JPH::CapsuleShapeSettings s(p->halfHeight * scale.y, p->radius * glm::max(scale.x, scale.z));

                auto r = s.Create();
                if (r.HasError()) { return nullptr; }
                result = r.Get();
                break;
            }

            case Physics::PhysicsShape::CYLINDER:
            {
                auto p = reinterpret_cast<const CylinderParams*>(params.data);
                if (!p) return nullptr;

                JPH::CylinderShapeSettings s(p->halfHeight * scale.y, p->radius * glm::max(scale.x, scale.z));

                auto r = s.Create();
                if (r.HasError()) { return nullptr; }
                result = r.Get();
                break;
            }

            case Physics::PhysicsShape::MESH:
            {
                Physics::PhysicsManager* physics = GetEngineContext()->GetPhysicsManager();
                Physics::MeshColliderCache& cache = physics->MeshColliders();

                Physics::MeshColliderSettings settings;
                if (!params.data || !FillMeshSettings(*reinterpret_cast<const MeshParams*>(params.data), settings))
                {
                    m_MeshMissing = true;
                    DEBUG_WARNING("PhysicsBody : the mesh shape of " + parent->GetName() + " has no mesh, and the actor has no model");
                    return nullptr;
                }

                Physics::MeshColliderResult built = cache.Acquire(settings, motion == EMotionType::Dynamic);
                if (!built.shape)
                {
                    DEBUG_ERROR("PhysicsBody : mesh shape of " + parent->GetName() + " : " + built.error);
                    return nullptr;
                }
                if (built.refining)
                    m_MeshRefining = true;

                JPH::Vec3 joltScale = ToJolt(scale);
                const bool unit = scale == glm::vec3(1.0f);
                if (unit)
                {
                    result = built.shape;
                    break;
                }
                if (!built.shape->IsValidScale(joltScale))
                    joltScale = built.shape->MakeScaleValid(joltScale);
                JPH::ScaledShapeSettings scaled(built.shape.GetPtr(), joltScale);
                auto r = scaled.Create();
                if (r.HasError()) { return nullptr; }
                result = r.Get();
                break;
            }

            default:
                return nullptr;
        }

        return result;
    }

    JPH::ShapeRefC PhysicsBody::BuildShape(EMotionType motion)
    {
        if (shapes.empty())
            return nullptr;

        m_MeshRefining = false;
        m_MeshMissing = false;

        std::vector<JPH::ShapeRefC> subShapes(shapes.size());
        for (size_t i = 0; i < shapes.size(); i++)
        {
            subShapes[i] = CreateJoltShape(shapes[i].shapeType, shapes[i].params, motion);
            if (!subShapes[i])
                return nullptr;
        }

        glm::vec3 scale = parent->transform->GetWorldScale();

        if (shapes.size() == 1)
        {
            const PhysicsShapeEntry& entry = shapes[0];

            bool identity = entry.offset == glm::vec3(0.0f) &&
                entry.rotation == glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

            if (identity)
                return subShapes[0];

            JPH::RotatedTranslatedShapeSettings wrap(ToJolt(entry.offset * scale), ToJolt(entry.rotation), subShapes[0].GetPtr());
            auto r = wrap.Create();
            return r.HasError() ? nullptr : r.Get();
        }

        JPH::StaticCompoundShapeSettings compound;
        for (size_t i = 0; i < shapes.size(); i++)
            compound.AddShape(ToJolt(shapes[i].offset * scale), ToJolt(shapes[i].rotation), subShapes[i].GetPtr());

        auto r = compound.Create();
        return r.HasError() ? nullptr : r.Get();
    }

    bool PhysicsBody::BuildInspectorPatch(Collision::Patch& patch) const
    {
        bool any = false;
        if (collisionMode != Physics::CollisionModeChoice::Inherit)
        {
            static constexpr Collision::Mode modes[] = { Collision::Mode::Solid, Collision::Mode::Solid, Collision::Mode::Sensor, Collision::Mode::QueryOnly, Collision::Mode::Disabled };
            patch.mode = modes[int(collisionMode)];
            any = true;
        }
        if (collisionDefault != Physics::CollisionResponseChoice::Inherit)
        {
            static constexpr Collision::Response responses[] = { Collision::Response::Ignore, Collision::Response::Ignore, Collision::Response::Overlap, Collision::Response::Block };
            patch.defaultResponse = responses[int(collisionDefault)];
            any = true;
        }
        if (!collisionIgnore.empty())
        {
            Core::TagQuery query;
            std::string error;
            if (Core::TagQuery::TryParse(collisionIgnore, query, &error))
            {
                patch.rules.push_back({ query, Collision::Response::Ignore });
                any = true;
            }
            else
            {
                DEBUG_WARNING("PhysicsBody : collision ignore query '" + collisionIgnore + "' : " + error);
            }
        }
        return any;
    }

    void PhysicsBody::RefreshCollision()
    {
        Physics::PhysicsManager* physics = GetEngineContext() ? GetEngineContext()->GetPhysicsManager() : nullptr;
        if (m_BodyID.IsInvalid() || !physics)
            return;
        physics->Collision().RemoveOverrides(m_BodyID, "inspector");
        Collision::Patch patch;
        if (BuildInspectorPatch(patch))
            physics->Collision().PushOverride(m_BodyID, "inspector", -1000, std::move(patch));
        std::string error;
        const char* fallback = motionType == EMotionType::Static ? Collision::kDefaultStaticProfile : Collision::kDefaultMovingProfile;
        physics->Collision().SetProfile(m_BodyID, collisionProfile.empty() ? std::string(fallback) : collisionProfile, &error);
    }

    bool PhysicsBody::FillMeshSettings(const MeshParams& p, Physics::MeshColliderSettings& settings) const
    {
        Physics::PhysicsManager* physics = GetEngineContext() ? GetEngineContext()->GetPhysicsManager() : nullptr;
        settings.mesh = p.mesh;
        if (settings.mesh.GetAsInt() == 0 && parent && physics && physics->MeshColliders().Source().modelMesh)
            settings.mesh = physics->MeshColliders().Source().modelMesh(*parent);
        if (settings.mesh.GetAsInt() == 0)
            return false;
        settings.approximation = p.approximation;
        settings.submeshMask = Physics::ParseSubmeshMask(p.submeshes);
        settings.convexRadius = p.convexRadius;
        settings.concavity = p.concavity;
        settings.maxHulls = p.maxHulls;
        settings.maxHullVertices = p.maxHullVertices;
        settings.edgeAngle = p.edgeAngle;
        return true;
    }

    bool PhysicsBody::GetMeshSettings(size_t shapeIndex, Physics::MeshColliderSettings& out) const
    {
        if (shapeIndex >= shapes.size() || shapes[shapeIndex].shapeType != Physics::PhysicsShape::MESH || !shapes[shapeIndex].params.data)
            return false;
        return FillMeshSettings(*reinterpret_cast<const MeshParams*>(shapes[shapeIndex].params.data), out);
    }

    bool PhysicsBody::UsesMeshAsset(Filesystem::AssetID asset) const
    {
        Physics::PhysicsManager* physics = GetEngineContext() ? GetEngineContext()->GetPhysicsManager() : nullptr;
        for (const PhysicsShapeEntry& entry : shapes)
        {
            if (entry.shapeType != Physics::PhysicsShape::MESH)
                continue;
            auto p = reinterpret_cast<const MeshParams*>(entry.params.data);
            if (!p)
                continue;
            if (p->mesh == asset)
                return true;
            if (p->mesh.GetAsInt() == 0 && parent && physics && physics->MeshColliders().Source().modelMesh &&
                physics->MeshColliders().Source().modelMesh(*parent) == asset)
                return true;
        }
        return false;
    }

    void PhysicsBody::WatchMesh()
    {
        Physics::PhysicsManager* physics = GetEngineContext() ? GetEngineContext()->GetPhysicsManager() : nullptr;
        if (!physics || !parent)
            return;
        if (m_MeshRefining && physics->MeshColliders().Generation() != m_MeshGeneration)
            shouldUpdateShape = true;
        if (m_MeshMissing)
        {
            const auto& source = physics->MeshColliders().Source();
            if (source.modelMesh && source.modelMesh(*parent).GetAsInt() != 0)
                shouldUpdateShape = true;
        }
    }

    void PhysicsBody::CreateBody(EMotionType newMotionType)
    {
        RemoveBody(Collision::EndCause::Teleported);

        m_Shape = BuildShape(newMotionType);
        m_MeshGeneration = GetEngineContext()->GetPhysicsManager()->MeshColliders().Generation();
        if (!m_Shape)
            return;

        // Build from the current shapes: a rebuild queued earlier would replace the body at the next Tick and drop impulses or velocity applied since.
        shouldUpdateShape = false;

        motionType = newMotionType;

        // Jolt simulates in world space, so create at the composed world pose. Dynamic bodies are converted back to local every tick.
        JPH::BodyCreationSettings settings(
            m_Shape,
            ToJolt(parent->transform->GetWorldPositionD()),
            ToJolt(parent->transform->GetWorldRotationD()),
            motionType,
            JPH::ObjectLayer(0)
        );

        settings.mLinearDamping = linearDamping;
        settings.mAngularDamping = angularDamping;
        settings.mGravityFactor = gravityFactor;
        settings.mFriction = friction;
        settings.mRestitution = restitution;

        if (overrideMass && motionType == EMotionType::Dynamic)
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = mass;
        }

        Physics::PhysicsManager* physics = GetEngineContext()->GetPhysicsManager();
        Collision::Patch patch;
        const bool hasPatch = BuildInspectorPatch(patch);
        Physics::ColliderState collider;
        std::string collisionError;
        if (!physics->Collision().MakeState(collisionProfile, motionType != EMotionType::Static, hasPatch ? &patch : nullptr, collider, &collisionError))
        {
            DEBUG_ERROR("PhysicsBody : " + collisionError);
            return;
        }
        physics->Collision().Configure(collider, settings, motionType);
        m_BodyID = physics->CreateBody(settings, this, std::move(collider));
        m_PoseValid = false;

        if (!m_BodyID.IsInvalid())
            GetEngineContext()->GetPhysicsManager()->SetBodyGravity(m_BodyID, gravityMode, customGravity);
    }

    void PhysicsBody::ApplyTransformToPhysics(float dt)
    {
        auto& bi = GetEngineContext()->GetPhysicsManager()->GetBodyInterface();

        if (motionType == EMotionType::Static)
        {
            CreateBody(EMotionType::Static);
        }
        else if (motionType == EMotionType::Kinematic)
        {
            bi.MoveKinematic(
                m_BodyID,
                ToJolt(parent->transform->GetWorldPositionD()),
                ToJolt(parent->transform->GetWorldRotationD()),
                dt
            );
        }
        else
        {
            bi.SetPositionAndRotation(
                m_BodyID,
                ToJolt(parent->transform->GetWorldPositionD()),
                ToJolt(parent->transform->GetWorldRotationD()),
                JPH::EActivation::Activate
            );
        }
    }

    void PhysicsBody::SyncTransformFromPhysics()
    {
        auto& bi = GetEngineContext()->GetPhysicsManager()->GetBodyInterface();

        // Use body position, not center of mass: shape offsets recenter around the COM and would drag the actor's origin. Result goes from root frame (double) to the actor's frame.
        glm::vec3 worldPos = parent->transform->RootToFramePosition(ToGLMD(bi.GetPosition(m_BodyID)));
        glm::quat worldRot = parent->transform->RootToFrameRotation(glm::dquat(ToGLM(bi.GetRotation(m_BodyID))));

        RecordPose(worldPos, worldRot);

        std::shared_ptr<Actor> parentActor = std::dynamic_pointer_cast<Actor>(parent->GetParent());

        if (!parentActor || !parentActor->transform)
        {
            parent->transform->SetPosition(worldPos, false);
            parent->transform->SetRotation(worldRot, false);
            return;
        }

        // Jolt result is world space: convert to parent-relative or the parent transform is applied twice. Scale is left alone.
        glm::mat4 parentWorld = parentActor->transform->GetWorldMatrix();
        glm::mat4 worldMatrix = glm::translate(glm::mat4(1.0f), worldPos) * glm::toMat4(worldRot);
        glm::mat4 localMatrix = glm::inverse(parentWorld) * worldMatrix;

        glm::mat3 localRotBasis(
            glm::normalize(glm::vec3(localMatrix[0])),
            glm::normalize(glm::vec3(localMatrix[1])),
            glm::normalize(glm::vec3(localMatrix[2]))
        );

        parent->transform->SetPosition(glm::vec3(localMatrix[3]), false);
        parent->transform->SetRotation(glm::quat_cast(localRotBasis), false);
    }

    void PhysicsBody::RecordPose(const glm::vec3& position, const glm::quat& rotation)
    {
        const uint64_t step = GetEngineContext()->GetPhysicsManager()->GetStepCount();

        if (!m_PoseValid)
        {
            m_PrevPosition = m_CurrPosition = position;
            m_PrevRotation = m_CurrRotation = rotation;
            m_PoseValid = true;
        }
        else
        {
            if (step != m_PoseStep)
            {
                m_PrevPosition = m_CurrPosition;
                m_PrevRotation = m_CurrRotation;
            }
            m_CurrPosition = position;
            m_CurrRotation = rotation;
        }
        m_PoseStep = step;
    }

    bool PhysicsBody::GetInterpolationOffset(float alpha, glm::mat4& offset) const
    {
        if (!m_PoseValid || !activated || motionType != EMotionType::Dynamic)
            return false;
        if (m_PrevPosition == m_CurrPosition && m_PrevRotation == m_CurrRotation)
            return false;

        offset = Physics::PoseInterpolationOffset(m_PrevPosition, m_PrevRotation, m_CurrPosition, m_CurrRotation, alpha);
        return true;
    }

    void PhysicsBody::Tick(float dt){

        if(!activated)
            return;

        const bool playing = GetEngineContext()->IsInPlayMode();
        const bool posDirty   = parent->transform->IsDirty(DirtyFlags::Position);
        const bool rotDirty   = parent->transform->IsDirty(DirtyFlags::Rotation);
        const bool scaleDirty = parent->transform->IsDirty(DirtyFlags::Scale);

        if (m_MeshRefining || m_MeshMissing)
            WatchMesh();

        if(shouldUpdateShape)
        {
            CreateBody(motionType);
            shouldUpdateShape = false;
        }

        if (!playing)
        {
            if (scaleDirty)
            {
                CreateBody(motionType);
            }

            if (posDirty || rotDirty)
            {
                ApplyTransformToPhysics(dt);
            }
        }
        else
        {
            if (motionType == EMotionType::Dynamic)
            {
                SyncTransformFromPhysics();
            }
            else if (motionType == EMotionType::Kinematic)
            {
                // A parent moving never sets this actor's dirty flags, so parented kinematic bodies are pushed every tick.
                const bool hasParent = std::dynamic_pointer_cast<Actor>(parent->GetParent()) != nullptr;

                if (posDirty || rotDirty || hasParent)
                {
                    ApplyTransformToPhysics(dt);
                }
            }
        }

        parent->transform->ClearDirty(DirtyFlags::All);
    }

    void PhysicsBody::SetPosition(glm::vec3 newPos)
    {
        SetPositionD(glm::dvec3(newPos));
    }

    void PhysicsBody::SetPositionD(const glm::dvec3& newPos)
    {
        if (m_BodyID.IsInvalid() || !activated)
            return;

        auto physics = GetEngineContext()->GetPhysicsManager();
        if (!physics)
            return;

        auto& bi = physics->GetPhysicsSystem().GetBodyInterface();

        JPH::RVec3 newPosition = ToJolt(newPos);


        if(motionType == EMotionType::Dynamic || motionType == EMotionType::Kinematic){
            m_PoseValid = false;
            physics->Collision().NotifyTeleport(m_BodyID);
            bi.SetPosition(
                m_BodyID,
                newPosition,
                JPH::EActivation::Activate
            );
        }
        else{
            CreateBody(motionType);
        }
    }

    void PhysicsBody::SetRotation(glm::vec3 newRot)
    {
        if(!activated)
            return;

        SetRotation(glm::quat(newRot));
    }

    void PhysicsBody::SetRotation(glm::quat newRot)
    {

        if (m_BodyID.IsInvalid() || !activated)
            return;

        auto physics = GetEngineContext()->GetPhysicsManager();
        if (!physics)
            return;

        auto& bi = physics->GetPhysicsSystem().GetBodyInterface();

        JPH::Quat joltRot = ToJolt(newRot);

        m_PoseValid = false;
        physics->Collision().NotifyTeleport(m_BodyID);
        bi.SetRotation(
            m_BodyID,
            joltRot,
            JPH::EActivation::Activate
        );
    }

    void PhysicsBody::Activate()
    {
        Component::Activate();

        Physics::PhysicsManager* physics = GetEngineContext()->GetPhysicsManager();
        physics->Collision().RemoveOverrides(m_BodyID, "deactivated");
        physics->GetBodyInterface().ActivateBody(m_BodyID);
    }

    void PhysicsBody::DeActivate()
    {
        Component::DeActivate();

        Physics::PhysicsManager* physics = GetEngineContext()->GetPhysicsManager();
        Collision::Patch off;
        off.mode = Collision::Mode::Disabled;
        physics->Collision().RemoveOverrides(m_BodyID, "deactivated");
        physics->Collision().PushOverride(m_BodyID, "deactivated", 1000000, std::move(off));
        physics->GetBodyInterface().DeactivateBody(m_BodyID);
    }

    void PhysicsBody::RemoveBody(Collision::EndCause cause)
    {
        if (!m_BodyID.IsInvalid()) {
            GetEngineContext()->GetPhysicsManager()->RemoveBody(m_BodyID, cause);
            m_BodyID = JPH::BodyID();
        }
    }

    void PhysicsBody::Deserialize(const json componentData)
    {
        const auto& parentName = parent ? parent->GetName() : "UNKNOWN";

        json data = componentData;

        if (data.is_object())
        {
            static const std::pair<const char*, const char*> legacyKeys[] = {
                { "motion_type", "motionType" },
                { "override_mass", "overrideMass" },
                { "linear_damping", "linearDamping" },
                { "angular_damping", "angularDamping" },
                { "gravity_factor", "gravityFactor" },
            };

            for (const auto& [legacy, key] : legacyKeys)
            {
                if (!data.contains(key) && data.contains(legacy))
                    data[key] = data[legacy];
            }

            if (data.contains("motionType") && data["motionType"].is_string())
            {
                std::string motion = data["motionType"].get<std::string>();
                if (!motion.empty())
                    motion[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(motion[0])));
                data["motionType"] = motion;
            }
        }

        DeserializeReflectedFields(data, false);

        std::vector<PhysicsShapeEntry> newShapes;

        auto parseOffsetRotation = [&](const json& entryJson, PhysicsShapeEntry& out)
        {
            if (entryJson.contains("offset") && entryJson["offset"].is_object())
            {
                const auto& o = entryJson["offset"];
                out.offset = glm::vec3(o.value("x", 0.0f), o.value("y", 0.0f), o.value("z", 0.0f));
            }

            if (entryJson.contains("rotation") && entryJson["rotation"].is_object())
            {
                const auto& r = entryJson["rotation"];
                out.rotation = glm::quat(r.value("w", 1.0f), r.value("x", 0.0f), r.value("y", 0.0f), r.value("z", 0.0f));
            }
        };

        if (data.contains("shapes") && data["shapes"].is_array())
        {
            for (const auto& entryJson : data["shapes"])
            {
                if (!entryJson.contains("shape") || !entryJson["shape"].is_string())
                {
                    DEBUG_ERROR("Missing shape type in shapes array for actor: " + (std::string)parentName);
                    return;
                }

                Physics::PhysicsShape shape;
                if (!ParseShapeTypeString(entryJson["shape"].get<std::string>(), shape))
                {
                    DEBUG_ERROR("Unknown physics shape for actor: " + (std::string)parentName);
                    return;
                }

                if (!entryJson.contains("params") || !entryJson["params"].is_object())
                {
                    DEBUG_ERROR("Missing physics params for actor: " + (std::string)parentName);
                    return;
                }

                PhysicsShapeEntry entry;
                entry.shapeType = shape;
                if (!ParseShapeParams(entryJson["params"], shape, entry.params, parentName, ContextFor(shape)))
                    return;

                parseOffsetRotation(entryJson, entry);

                newShapes.push_back(std::move(entry));
            }
        }
        else
        {
            if (!data.contains("shape") || !data["shape"].is_string())
            {
                DEBUG_ERROR("Missing physics shape for actor: " + (std::string)parentName);
                return;
            }

            const std::string shapeStr = data["shape"].get<std::string>();

            Physics::PhysicsShape shape;
            if (!ParseShapeTypeString(shapeStr, shape))
            {
                DEBUG_ERROR("Unknown physics shape: " + shapeStr);
                return;
            }

            if (!data.contains("params") || !data["params"].is_object())
            {
                DEBUG_ERROR("Missing physics params for actor: " + (std::string)parentName);
                return;
            }

            PhysicsShapeEntry entry;
            entry.shapeType = shape;
            if (!ParseShapeParams(data["params"], shape, entry.params, parentName, ContextFor(shape)))
                return;

            newShapes.push_back(std::move(entry));
        }

        if (newShapes.empty())
        {
            DEBUG_ERROR("No physics shapes for actor: " + (std::string)parentName);
            return;
        }

        shapes = std::move(newShapes);

        CreateBody(motionType);

        Activate();
        DeserializeActive(data, true);
    }

    ordered_json PhysicsBody::Serialize()
    {
        ordered_json comp = SerializeReflected("physics_body");

        comp["shapes"] = ordered_json::array();

        for (const auto& entry : shapes)
        {
            ordered_json shapeJson;
            shapeJson["shape"] = ShapeTypeToString(entry.shapeType);

            ordered_json paramsJson = ordered_json::object();
            SerializeShapeParams(paramsJson, entry.params, ContextFor(entry.shapeType));
            shapeJson["params"] = paramsJson;

            shapeJson["offset"]["x"] = entry.offset.x;
            shapeJson["offset"]["y"] = entry.offset.y;
            shapeJson["offset"]["z"] = entry.offset.z;

            shapeJson["rotation"]["x"] = entry.rotation.x;
            shapeJson["rotation"]["y"] = entry.rotation.y;
            shapeJson["rotation"]["z"] = entry.rotation.z;
            shapeJson["rotation"]["w"] = entry.rotation.w;

            comp["shapes"].push_back(shapeJson);
        }

        return comp;
    }

    std::shared_ptr<Component> PhysicsBody::Clone() const
    {
        auto cloned = Object::Create<PhysicsBody>(*this);
        cloned->m_BodyID = JPH::BodyID();

        cloned->CreateBody(motionType);
        return cloned;
    }

    void PhysicsBody::AddShape(Physics::PhysicsShape type)
    {
        PhysicsShapeEntry entry;
        entry.shapeType = type;
        InitShapeParams(entry.params, type);

        shapes.push_back(std::move(entry));
        shouldUpdateShape = true;
    }

    void PhysicsBody::RemoveShape(size_t index)
    {
        if (shapes.size() <= 1 || index >= shapes.size())
            return;

        shapes.erase(shapes.begin() + index);
        shouldUpdateShape = true;
    }

    void PhysicsBody::SetShapeType(size_t index, Physics::PhysicsShape newType)
    {
        if (index >= shapes.size())
            return;

        PhysicsShapeEntry& entry = shapes[index];
        entry.shapeType = newType;
        InitShapeParams(entry.params, newType);

        shouldUpdateShape = true;
    }

    glm::vec3 PhysicsBody::GetPosition() const
    {
        if (m_BodyID.IsInvalid())
            return parent ? parent->transform->GetWorldPosition() : glm::vec3(0.0f);

        return ToGLM(GetEngineContext()->GetPhysicsManager()->GetBodyInterface().GetPosition(m_BodyID));
    }

    glm::dvec3 PhysicsBody::GetPositionD() const
    {
        if (m_BodyID.IsInvalid())
            return parent ? parent->transform->GetWorldPositionD() : glm::dvec3(0.0);

        return ToGLMD(GetEngineContext()->GetPhysicsManager()->GetBodyInterface().GetPosition(m_BodyID));
    }

    glm::quat PhysicsBody::GetRotation() const
    {
        if (m_BodyID.IsInvalid())
            return parent ? parent->transform->GetWorldRotationQuat() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        return ToGLM(GetEngineContext()->GetPhysicsManager()->GetBodyInterface().GetRotation(m_BodyID));
    }

    glm::vec3 PhysicsBody::GetCenterOfMassPosition() const
    {
        if (m_BodyID.IsInvalid())
            return glm::vec3(0.0f);

        return ToGLM(GetEngineContext()->GetPhysicsManager()->GetBodyInterface().GetCenterOfMassPosition(m_BodyID));
    }

    void PhysicsBody::SetLinearVelocity(glm::vec3 velocity)
    {
        if (m_BodyID.IsInvalid() || !activated)
            return;

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().SetLinearVelocity(m_BodyID, ToJolt(velocity));
    }

    glm::vec3 PhysicsBody::GetLinearVelocity() const
    {
        if (m_BodyID.IsInvalid())
            return glm::vec3(0.0f);

        return ToGLM(GetEngineContext()->GetPhysicsManager()->GetBodyInterface().GetLinearVelocity(m_BodyID));
    }

    void PhysicsBody::SetAngularVelocity(glm::vec3 velocity)
    {
        if (m_BodyID.IsInvalid() || !activated)
            return;

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().SetAngularVelocity(m_BodyID, ToJolt(velocity));
    }

    glm::vec3 PhysicsBody::GetAngularVelocity() const
    {
        if (m_BodyID.IsInvalid())
            return glm::vec3(0.0f);

        return ToGLM(GetEngineContext()->GetPhysicsManager()->GetBodyInterface().GetAngularVelocity(m_BodyID));
    }

    void PhysicsBody::AddForce(glm::vec3 force)
    {
        if (m_BodyID.IsInvalid() || !activated)
            return;

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().AddForce(m_BodyID, ToJolt(force));
    }

    void PhysicsBody::AddForceAtPoint(glm::vec3 force, glm::vec3 point)
    {
        if (m_BodyID.IsInvalid() || !activated)
            return;

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().AddForce(m_BodyID, ToJolt(force), ToJolt(glm::dvec3(point)));
    }

    void PhysicsBody::AddTorque(glm::vec3 torque)
    {
        if (m_BodyID.IsInvalid() || !activated)
            return;

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().AddTorque(m_BodyID, ToJolt(torque));
    }

    void PhysicsBody::AddImpulse(glm::vec3 impulse)
    {
        if (m_BodyID.IsInvalid() || !activated)
            return;

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().AddImpulse(m_BodyID, ToJolt(impulse));
    }

    void PhysicsBody::AddImpulseAtPoint(glm::vec3 impulse, glm::vec3 point)
    {
        if (m_BodyID.IsInvalid() || !activated)
            return;

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().AddImpulse(m_BodyID, ToJolt(impulse), ToJolt(glm::dvec3(point)));
    }

    void PhysicsBody::AddAngularImpulse(glm::vec3 impulse)
    {
        if (m_BodyID.IsInvalid() || !activated)
            return;

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().AddAngularImpulse(m_BodyID, ToJolt(impulse));
    }

    void PhysicsBody::ApplyMassProperties()
    {
        if (m_BodyID.IsInvalid() || !activated || motionType != EMotionType::Dynamic || !m_Shape)
            return;

        auto physics = GetEngineContext()->GetPhysicsManager();
        JPH::BodyLockWrite lock(physics->GetPhysicsSystem().GetBodyLockInterface(), m_BodyID);
        if (!lock.Succeeded())
            return;

        JPH::MotionProperties* mp = lock.GetBody().GetMotionPropertiesUnchecked();
        if (!mp)
            return;

        JPH::MassProperties massProps = m_Shape->GetMassProperties();
        if (overrideMass)
            massProps.ScaleToMass(mass);

        mp->SetMassProperties(mp->GetAllowedDOFs(), massProps);
    }

    void PhysicsBody::SetMass(float newMass)
    {
        mass = newMass;
        overrideMass = true;
        ApplyMassProperties();
    }

    float PhysicsBody::GetMass() const
    {
        if (m_BodyID.IsInvalid() || motionType != EMotionType::Dynamic)
            return mass;

        auto physics = GetEngineContext()->GetPhysicsManager();
        JPH::BodyLockRead lock(physics->GetPhysicsSystem().GetBodyLockInterface(), m_BodyID);
        if (!lock.Succeeded())
            return mass;

        const JPH::MotionProperties* mp = lock.GetBody().GetMotionPropertiesUnchecked();
        if (!mp)
            return mass;

        float invMass = mp->GetInverseMassUnchecked();
        return invMass > 0.0f ? 1.0f / invMass : mass;
    }

    void PhysicsBody::SetLinearDamping(float damping)
    {
        linearDamping = damping;

        if (m_BodyID.IsInvalid() || !activated)
            return;

        auto physics = GetEngineContext()->GetPhysicsManager();
        JPH::BodyLockWrite lock(physics->GetPhysicsSystem().GetBodyLockInterface(), m_BodyID);
        if (!lock.Succeeded())
            return;

        JPH::MotionProperties* mp = lock.GetBody().GetMotionPropertiesUnchecked();
        if (mp)
            mp->SetLinearDamping(damping);
    }

    float PhysicsBody::GetLinearDamping() const
    {
        if (m_BodyID.IsInvalid())
            return linearDamping;

        auto physics = GetEngineContext()->GetPhysicsManager();
        JPH::BodyLockRead lock(physics->GetPhysicsSystem().GetBodyLockInterface(), m_BodyID);
        if (!lock.Succeeded())
            return linearDamping;

        const JPH::MotionProperties* mp = lock.GetBody().GetMotionPropertiesUnchecked();
        return mp ? mp->GetLinearDamping() : linearDamping;
    }

    void PhysicsBody::SetAngularDamping(float damping)
    {
        angularDamping = damping;

        if (m_BodyID.IsInvalid() || !activated)
            return;

        auto physics = GetEngineContext()->GetPhysicsManager();
        JPH::BodyLockWrite lock(physics->GetPhysicsSystem().GetBodyLockInterface(), m_BodyID);
        if (!lock.Succeeded())
            return;

        JPH::MotionProperties* mp = lock.GetBody().GetMotionPropertiesUnchecked();
        if (mp)
            mp->SetAngularDamping(damping);
    }

    float PhysicsBody::GetAngularDamping() const
    {
        if (m_BodyID.IsInvalid())
            return angularDamping;

        auto physics = GetEngineContext()->GetPhysicsManager();
        JPH::BodyLockRead lock(physics->GetPhysicsSystem().GetBodyLockInterface(), m_BodyID);
        if (!lock.Succeeded())
            return angularDamping;

        const JPH::MotionProperties* mp = lock.GetBody().GetMotionPropertiesUnchecked();
        return mp ? mp->GetAngularDamping() : angularDamping;
    }

    void PhysicsBody::SetGravityFactor(float factor)
    {
        gravityFactor = factor;

        if (m_BodyID.IsInvalid() || !activated)
            return;

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().SetGravityFactor(m_BodyID, factor);
    }

    float PhysicsBody::GetGravityFactor() const
    {
        if (m_BodyID.IsInvalid())
            return gravityFactor;

        return GetEngineContext()->GetPhysicsManager()->GetBodyInterface().GetGravityFactor(m_BodyID);
    }

    void PhysicsBody::SetGravityMode(Physics::GravityMode mode)
    {
        gravityMode = mode;

        if (m_BodyID.IsInvalid())
            return;

        GetEngineContext()->GetPhysicsManager()->SetBodyGravity(m_BodyID, gravityMode, customGravity);

        if (activated && motionType == EMotionType::Dynamic)
            GetEngineContext()->GetPhysicsManager()->GetBodyInterface().ActivateBody(m_BodyID);
    }

    void PhysicsBody::SetCustomGravity(glm::vec3 gravity)
    {
        customGravity = gravity;
        SetGravityMode(Physics::GravityMode::Custom);
    }

    glm::vec3 PhysicsBody::GetGravity() const
    {
        auto physics = GetEngineContext() ? GetEngineContext()->GetPhysicsManager() : nullptr;
        if (m_BodyID.IsInvalid() || !physics)
            return glm::vec3(0.0f);

        return physics->GetBodyGravity(m_BodyID) * GetGravityFactor();
    }

    void PhysicsBody::SetFriction(float newFriction)
    {
        friction = newFriction;

        if (m_BodyID.IsInvalid() || !activated)
            return;

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().SetFriction(m_BodyID, newFriction);
    }

    float PhysicsBody::GetFriction() const
    {
        if (m_BodyID.IsInvalid())
            return friction;

        return GetEngineContext()->GetPhysicsManager()->GetBodyInterface().GetFriction(m_BodyID);
    }

    void PhysicsBody::SetRestitution(float newRestitution)
    {
        restitution = newRestitution;

        if (m_BodyID.IsInvalid() || !activated)
            return;

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().SetRestitution(m_BodyID, newRestitution);
    }

    float PhysicsBody::GetRestitution() const
    {
        if (m_BodyID.IsInvalid())
            return restitution;

        return GetEngineContext()->GetPhysicsManager()->GetBodyInterface().GetRestitution(m_BodyID);
    }

    void PhysicsBody::OnFieldChanged(const FieldChangedEvent& event){
        const char* name = event.field->name;

        if (strcmp(name, "motionType") == 0)
        {
            Core::IEngineContext* engine = GetEngineContext();
            if (activated && parent && engine && engine->GetPhysicsManager())
                CreateBody(motionType);
            else
                shouldUpdateShape = true;
        }
        else if (strcmp(name, "overrideMass") == 0 || strcmp(name, "mass") == 0)
        {
            ApplyMassProperties();
        }
        else if (strcmp(name, "linearDamping") == 0)
        {
            SetLinearDamping(linearDamping);
        }
        else if (strcmp(name, "angularDamping") == 0)
        {
            SetAngularDamping(angularDamping);
        }
        else if (strcmp(name, "gravityFactor") == 0)
        {
            SetGravityFactor(gravityFactor);
        }
        else if (strcmp(name, "gravityMode") == 0 || strcmp(name, "customGravity") == 0)
        {
            SetGravityMode(gravityMode);
        }
        else if (strcmp(name, "collisionProfile") == 0 || strcmp(name, "collisionMode") == 0 || strcmp(name, "collisionDefault") == 0 || strcmp(name, "collisionIgnore") == 0)
        {
            RefreshCollision();
        }
        else if (strcmp(name, "friction") == 0)
        {
            SetFriction(friction);
        }
        else if (strcmp(name, "restitution") == 0)
        {
            SetRestitution(restitution);
        }
    }
}

#include "physics_body.hpp"

#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"

#include "physics_body.reflection.hpp"

#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Body/BodyLock.h>

#include <glm/gtc/matrix_transform.hpp>

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
            }
        }

        bool ParseShapeTypeString(const std::string& s, Physics::PhysicsShape& out)
        {
            if (s == "box") { out = Physics::PhysicsShape::BOX; return true; }
            if (s == "sphere") { out = Physics::PhysicsShape::SPHERE; return true; }
            if (s == "capsule") { out = Physics::PhysicsShape::CAPSULE; return true; }
            if (s == "cylinder") { out = Physics::PhysicsShape::CYLINDER; return true; }
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
            }
            return "box";
        }

        bool ParseShapeParams(const json& p, Physics::PhysicsShape shape, InstancedStruct& outParams, const std::string& parentName)
        {
            auto requireFloat = [&](const char* key, std::optional<float>& out) -> bool
            {
                if (!p.contains(key) || !p[key].is_number())
                    return false;
                out = p[key].get<float>();
                return true;
            };

            switch (shape)
            {
                case Physics::PhysicsShape::BOX:
                {
                    std::optional<float> x, y, z;
                    if (!requireFloat("x", x) || !requireFloat("y", y) || !requireFloat("z", z))
                    {
                        DEBUG_ERROR("Invalid BOX params for actor: " + parentName);
                        return false;
                    }

                    outParams.Initialize(&BoxParams_descriptor);
                    auto& vec = *reinterpret_cast<glm::vec3*>(outParams.data);
                    vec = glm::vec3(*x, *y, *z);
                    return true;
                }

                case Physics::PhysicsShape::SPHERE:
                {
                    std::optional<float> radius;
                    if (!requireFloat("radius", radius))
                    {
                        DEBUG_ERROR("Invalid SPHERE params for actor: " + parentName);
                        return false;
                    }

                    outParams.Initialize(&SphereParams_descriptor);
                    *reinterpret_cast<float*>(outParams.data) = *radius;
                    return true;
                }

                case Physics::PhysicsShape::CAPSULE:
                {
                    std::optional<float> radius, halfHeight;
                    if (!requireFloat("radius", radius) || !requireFloat("halfHeight", halfHeight))
                    {
                        DEBUG_ERROR("Invalid CAPSULE params for actor: " + parentName);
                        return false;
                    }

                    outParams.Initialize(&CapsuleParams_descriptor);
                    auto* dataPtr = reinterpret_cast<CapsuleParams*>(outParams.data);
                    dataPtr->radius = *radius;
                    dataPtr->halfHeight = *halfHeight;
                    return true;
                }

                case Physics::PhysicsShape::CYLINDER:
                {
                    std::optional<float> radius, halfHeight;
                    if (!requireFloat("radius", radius) || !requireFloat("halfHeight", halfHeight))
                    {
                        DEBUG_ERROR("Invalid CYLINDER params for actor: " + parentName);
                        return false;
                    }

                    outParams.Initialize(&CylinderParams_descriptor);
                    auto* dataPtr = reinterpret_cast<CylinderParams*>(outParams.data);
                    dataPtr->radius = *radius;
                    dataPtr->halfHeight = *halfHeight;
                    return true;
                }
            }

            DEBUG_ERROR("Unhandled physics shape for actor: " + parentName);
            return false;
        }

        void SerializeShapeParams(ordered_json& out, Physics::PhysicsShape shape, const InstancedStruct& params)
        {
            switch (shape)
            {
                case Physics::BOX:
                {
                    auto p = reinterpret_cast<const BoxParams*>(params.data);
                    out["x"] = p->halfExtent.x;
                    out["y"] = p->halfExtent.y;
                    out["z"] = p->halfExtent.z;
                    break;
                }

                case Physics::SPHERE:
                {
                    auto p = reinterpret_cast<const SphereParams*>(params.data);
                    out["radius"] = p->radius;
                    break;
                }

                case Physics::CAPSULE:
                {
                    auto p = reinterpret_cast<const CapsuleParams*>(params.data);
                    out["radius"] = p->radius;
                    out["halfHeight"] = p->halfHeight;
                    break;
                }

                case Physics::CYLINDER:
                {
                    auto p = reinterpret_cast<const CylinderParams*>(params.data);
                    out["radius"] = p->radius;
                    out["halfHeight"] = p->halfHeight;
                    break;
                }
            }
        }
    }

    PhysicsBody::PhysicsBody(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {
        shapes.emplace_back();
        shapes[0].params.Initialize(&BoxParams_descriptor);
    }

    JPH::ShapeRefC PhysicsBody::CreateJoltShape(Physics::PhysicsShape shape, const InstancedStruct& params, size_t index)
    {
        // Jolt shapes have no scale of their own - their dimensions must be baked in at creation time
        // from the actor's composed world scale (like Unity sizing colliders from lossyScale), not just
        // its own local scale, otherwise a shape parented under a scaled actor is built at the wrong size.
        glm::vec3 scale = parent->transform->GetWorldScale();

        Core::DebugShapeDesc debugShape;
        JPH::ShapeRefC result;

        switch (shape)
        {
            case Physics::PhysicsShape::SPHERE:
            {
                auto p = reinterpret_cast<const SphereParams*>(params.data);
                if (!p) return nullptr;

                float size = std::max({ scale.x, scale.y, scale.z });

                JPH::SphereShapeSettings s(p->radius * size);
                debugShape.kind = Core::DebugShapeKind::Sphere;
                debugShape.radius = p->radius;

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

                debugShape.kind = Core::DebugShapeKind::Box;
                debugShape.halfExtent = p->halfExtent;

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

                debugShape.kind = Core::DebugShapeKind::Capsule;
                debugShape.radius = p->radius;
                debugShape.halfHeight = p->halfHeight;

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

                debugShape.kind = Core::DebugShapeKind::Cylinder;
                debugShape.radius = p->radius;
                debugShape.halfHeight = p->halfHeight;

                auto r = s.Create();
                if (r.HasError()) { return nullptr; }
                result = r.Get();
                break;
            }

            default:
                return nullptr;
        }

        if (index >= m_DebugShapes.size())
            m_DebugShapes.resize(index + 1, 0);

        Core::IDebugDraw* debugDraw = GetEngineContext()->GetDebugDraw();
        debugDraw->DestroyShape(m_DebugShapes[index]);
        m_DebugShapes[index] = debugDraw->CreateShape(debugShape);

        return result;
    }

    JPH::ShapeRefC PhysicsBody::BuildShape()
    {
        if (shapes.empty())
            return nullptr;

        std::vector<JPH::ShapeRefC> subShapes(shapes.size());
        for (size_t i = 0; i < shapes.size(); i++)
        {
            subShapes[i] = CreateJoltShape(shapes[i].shapeType, shapes[i].params, i);
            if (!subShapes[i])
                return nullptr;
        }

        // Must match CreateJoltShape()'s choice of scale - this one pre-scales each sub-shape's offset
        // for the same reason (Jolt's compound/rotated-translated shapes don't apply scale themselves).
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

    void PhysicsBody::CreateBody(EMotionType newMotionType)
    {
        RemoveBody();

        m_Shape = BuildShape();
        if (!m_Shape)
            return;

        motionType = newMotionType;

        // Jolt only ever simulates in world space, regardless of motion type, so bodies are always
        // created at the actor's composed world pose - a parented static prop or physics object sits
        // where it visually appears in the hierarchy. For Dynamic bodies, SyncTransformFromPhysics
        // converts Jolt's world result back to a parent-relative local value every tick, so this isn't
        // a one-time snapshot that goes stale if the parent later moves.
        JPH::BodyCreationSettings settings(
            m_Shape,
            ToJolt(parent->transform->GetWorldPosition()),
            ToJolt(parent->transform->GetWorldRotationQuat()),
            motionType,
            motionType == EMotionType::Static
                ? Physics::Layers::NON_MOVING
                : Physics::Layers::MOVING
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

        m_BodyID = GetEngineContext()->GetPhysicsManager()->CreateBody(settings, this);
    }

    void PhysicsBody::ApplyTransformToPhysics(float dt)
    {
        auto& bi = GetEngineContext()->GetPhysicsManager()->GetBodyInterface();

        if (motionType == EMotionType::Static)
        {
            // Editor-only: recreate
            CreateBody(EMotionType::Static);
        }
        else if (motionType == EMotionType::Kinematic)
        {
            bi.MoveKinematic(
                m_BodyID,
                ToJolt(parent->transform->GetWorldPosition()),
                ToJolt(parent->transform->GetWorldRotationQuat()),
                dt
            );
        }
        else // Dynamic (editor only)
        {
            bi.SetPositionAndRotation(
                m_BodyID,
                ToJolt(parent->transform->GetWorldPosition()),
                ToJolt(parent->transform->GetWorldRotationQuat()),
                JPH::EActivation::Activate
            );
        }
    }

    void PhysicsBody::SyncTransformFromPhysics()
    {
        auto& bi = GetEngineContext()->GetPhysicsManager()->GetBodyInterface();

        // Body position/rotation (not the center of mass!) is what corresponds to the actor's
        // transform - Jolt tracks the two separately precisely so that a shape offset (single-shape
        // RotatedTranslatedShape, or a multi-shape compound, both of which recenter internally
        // around the center of mass) doesn't leak into where the "origin" of the body is. Using
        // GetCenterOfMassPosition() here would drag the actor's transform towards the shape's COM
        // instead of its own origin whenever a shape has an offset or there's more than one shape.
        glm::vec3 worldPos = ToGLM(bi.GetPosition(m_BodyID));
        glm::quat worldRot = ToGLM(bi.GetRotation(m_BodyID));

        std::shared_ptr<Actor> parentActor = std::dynamic_pointer_cast<Actor>(parent->GetParent());

        if (!parentActor || !parentActor->transform)
        {
            parent->transform->SetPosition(worldPos, false);
            parent->transform->SetRotation(worldRot, false);
            return;
        }

        // Jolt only knows world space and has no idea this actor is parented, so its result has to be
        // converted back into a parent-relative local value before being written into position/rotation
        // - otherwise rendering (which composes local against the parent's world matrix) would apply
        // the parent's transform a second time on top of an already-world value. Scale is deliberately
        // left untouched: Jolt bodies carry no scale of their own to report back.
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

    void PhysicsBody::Tick(float dt){

        if(!activated)
            return;

        const bool playing = GetEngineContext()->IsInPlayMode();
        const bool posDirty   = parent->transform->IsDirty(DirtyFlags::Position);
        const bool rotDirty   = parent->transform->IsDirty(DirtyFlags::Rotation);
        const bool scaleDirty = parent->transform->IsDirty(DirtyFlags::Scale);

        bool shouldUpdateDrawCmd = false;

        if(shouldUpdateShape)
        {
            CreateBody(motionType);
            shouldUpdateShape = false;
            shouldUpdateDrawCmd = true;
        }

        if (!playing)
        {
            //EDITOR

            if (scaleDirty)
            {
                CreateBody(motionType);
                shouldUpdateDrawCmd = true;
            }

            if (posDirty || rotDirty)
            {
                ApplyTransformToPhysics(dt);
                shouldUpdateDrawCmd = true;
            }
        }
        else
        {
            // GAME
            if (motionType == EMotionType::Dynamic)
            {
                SyncTransformFromPhysics();
                shouldUpdateDrawCmd = true;
            }
            else if (motionType == EMotionType::Kinematic)
            {
                // A parented kinematic body's world pose can change purely because an ancestor moved,
                // which never touches this actor's own local dirty flags - so a parented body has to
                // be pushed to Jolt every tick rather than only when posDirty/rotDirty fires.
                const bool hasParent = std::dynamic_pointer_cast<Actor>(parent->GetParent()) != nullptr;

                if (posDirty || rotDirty || hasParent)
                {
                    ApplyTransformToPhysics(dt);
                    shouldUpdateDrawCmd = true;
                }
            }
        }

        if(shouldUpdateDrawCmd && !m_DebugShapes.empty()){
            std::vector<Core::DebugDrawItem> items;
            items.reserve(m_DebugShapes.size());

            for (size_t i = 0; i < m_DebugShapes.size(); i++)
            {
                if (m_DebugShapes[i] == 0)
                    continue;

                Core::DebugDrawItem item;
                item.shape = m_DebugShapes[i];
                item.ownerID = parent->GetComponentIDInWorld(local_id);

                // Each shape's debug mesh is generated in its own local space (unscaled, uncentered),
                // so fold that shape's offset/rotation into the model matrix on top of the actor's
                // transform - this has to match how BuildShape() places the same shape in Jolt (there,
                // the offset is pre-scaled and handed to Jolt's compound/rotated-translated shape; here,
                // the actor's transform matrix already carries that same scale, so composing with the
                // raw offset lands in the same place).
                glm::mat4 localOffset(1.0f);
                if (i < shapes.size())
                {
                    localOffset = glm::translate(glm::mat4(1.0f), shapes[i].offset) * glm::mat4_cast(shapes[i].rotation);
                }

                item.model = parent->transform->GetWorldMatrix() * localOffset;
                item.objectID = parent->GetID().GetAsInt();

                items.push_back(item);
            }

            if (!items.empty())
                GetEngineContext()->GetDebugDraw()->Draw(Core::DebugDrawLayer::Physics, items);
        }

        parent->transform->ClearDirty(DirtyFlags::All);
    }

    void PhysicsBody::SetPosition(glm::vec3 newPos)
    {
        if (m_BodyID.IsInvalid() || !activated)
            return;

        auto physics = GetEngineContext()->GetPhysicsManager();
        if (!physics)
            return;

        auto& bi = physics->GetPhysicsSystem().GetBodyInterface();

        JPH::RVec3 newPosition = ToJolt(newPos);


        if(motionType == EMotionType::Dynamic || motionType == EMotionType::Kinematic){
            // Teleport dynamic/kinematic body
            bi.SetPosition(
                m_BodyID,
                newPosition,
                JPH::EActivation::Activate
            );
        }
        else{
            // Recreate the static body at a new pos
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

        bi.SetRotation(
            m_BodyID,
            joltRot,
            JPH::EActivation::Activate
        );
    }

    void PhysicsBody::Activate()
    {
        Component::Activate();

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().ActivateBody(m_BodyID);
    }

    void PhysicsBody::DeActivate()
    {
        Component::DeActivate();

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().DeactivateBody(m_BodyID);
    }

    void PhysicsBody::RemoveBody()
    {
        if (!m_BodyID.IsInvalid()) {
            GetEngineContext()->GetPhysicsManager()->RemoveBody(m_BodyID);
            m_BodyID = JPH::BodyID();
        }

        for (Core::DebugShapeHandle shape : m_DebugShapes)
        {
            if (shape != 0)
                GetEngineContext()->GetDebugDraw()->DestroyShape(shape);
        }

        m_DebugShapes.clear();
    }

    void PhysicsBody::Deserialize(const json componentData)
    {
        auto getString = [&](const char* key) -> std::optional<std::string>
        {
            if (!componentData.contains(key) || !componentData[key].is_string())
                return std::nullopt;
            return componentData[key].get<std::string>();
        };

        auto getBool = [&](const char* key, bool defaultValue = false) -> bool
        {
            if (!componentData.contains(key) || !componentData[key].is_boolean())
                return defaultValue;
            return componentData[key].get<bool>();
        };

        auto getFloat = [&](const char* key, float defaultValue) -> float
        {
            if (!componentData.contains(key) || !componentData[key].is_number())
                return defaultValue;
            return componentData[key].get<float>();
        };

        const auto& parentName = parent ? parent->GetName() : "UNKNOWN";

        // ---------------- MOTION ----------------
        auto motionStr = getString("motion_type");
        if (!motionStr)
        {
            DEBUG_ERROR("Missing motion type for actor: " + (std::string)parentName);
            return;
        }

        JPH::EMotionType motion;

        if (*motionStr == "dynamic") motion = JPH::EMotionType::Dynamic;
        else if (*motionStr == "kinematic") motion = JPH::EMotionType::Kinematic;
        else if (*motionStr == "static") motion = JPH::EMotionType::Static;
        else
        {
            DEBUG_ERROR("Unknown motion type: " + *motionStr);
            return;
        }

        // ---------------- ATTRIBUTES ----------------
        overrideMass = getBool("override_mass", false);
        mass = getFloat("mass", 1.0f);
        linearDamping = getFloat("linear_damping", 0.05f);
        angularDamping = getFloat("angular_damping", 0.05f);
        gravityFactor = getFloat("gravity_factor", 1.0f);
        friction = getFloat("friction", 0.2f);
        restitution = getFloat("restitution", 0.0f);

        // ---------------- SHAPES ----------------
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

        if (componentData.contains("shapes") && componentData["shapes"].is_array())
        {
            for (const auto& entryJson : componentData["shapes"])
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
                if (!ParseShapeParams(entryJson["params"], shape, entry.params, parentName))
                    return;

                parseOffsetRotation(entryJson, entry);

                newShapes.push_back(std::move(entry));
            }
        }
        else
        {
            // Back-compat: pre-multi-shape format ("shape"/"params" directly on the component)
            auto shapeStr = getString("shape");
            if (!shapeStr)
            {
                DEBUG_ERROR("Missing physics shape for actor: " + (std::string)parentName);
                return;
            }

            Physics::PhysicsShape shape;
            if (!ParseShapeTypeString(*shapeStr, shape))
            {
                DEBUG_ERROR("Unknown physics shape: " + *shapeStr);
                return;
            }

            if (!componentData.contains("params") || !componentData["params"].is_object())
            {
                DEBUG_ERROR("Missing physics params for actor: " + (std::string)parentName);
                return;
            }

            PhysicsShapeEntry entry;
            entry.shapeType = shape;
            if (!ParseShapeParams(componentData["params"], shape, entry.params, parentName))
                return;

            newShapes.push_back(std::move(entry));
        }

        if (newShapes.empty())
        {
            DEBUG_ERROR("No physics shapes for actor: " + (std::string)parentName);
            return;
        }

        shapes = std::move(newShapes);

        // ---------------- CREATE BODY ----------------
        CreateBody(motion);

        // ---------------- ACTIVE STATE ----------------
        Activate();
        if (componentData.contains("active") && componentData["active"].is_boolean())
        {
            if (!componentData["active"].get<bool>())
                DeActivate();
        }
    }

    ordered_json PhysicsBody::Serialize()
    {
        ordered_json comp;

        comp["type"] = "physics_body";

        comp["active"] = activated;

        switch(motionType){
            case JPH::EMotionType::Dynamic:{
                comp["motion_type"] = "dynamic";
                break;
            }
            case JPH::EMotionType::Static:{
                comp["motion_type"] = "static";
                break;
            }
            case JPH::EMotionType::Kinematic:{
                comp["motion_type"] = "kinematic";
                break;
            }
        }

        comp["override_mass"] = overrideMass;
        comp["mass"] = mass;
        comp["linear_damping"] = linearDamping;
        comp["angular_damping"] = angularDamping;
        comp["gravity_factor"] = gravityFactor;
        comp["friction"] = friction;
        comp["restitution"] = restitution;

        comp["shapes"] = ordered_json::array();

        for (const auto& entry : shapes)
        {
            ordered_json shapeJson;
            shapeJson["shape"] = ShapeTypeToString(entry.shapeType);

            ordered_json paramsJson;
            SerializeShapeParams(paramsJson, entry.shapeType, entry.params);
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

        // The copy above shallow-copied our debug shape pointers - they still belong to `this`.
        // Clear the clone's list (without deleting) so CreateBody() below builds its own instead
        // of both objects pointing at (and eventually double-deleting) the same debug shapes.
        cloned->m_DebugShapes.clear();

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

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().AddForce(m_BodyID, ToJolt(force), ToJolt(point));
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

        GetEngineContext()->GetPhysicsManager()->GetBodyInterface().AddImpulse(m_BodyID, ToJolt(impulse), ToJolt(point));
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

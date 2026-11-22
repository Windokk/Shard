#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/Body.h>

#include "engine/physics/physics_manager.hpp"
#include "engine/physics/gravity.hpp"
#include "engine/physics/collision/collision_choices.hpp"
#include "engine/physics/mesh/mesh_collider.hpp"
#include "engine/assets/assetID.hpp"

#include "engine/world/components/component.hpp"

#include "engine/core/color.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/assets/reflection/attributes.hpp"
#include "engine/assets/serialization/reflection/reflection_serializer.hpp"


namespace Shard::Engine::Physics
{
    // Rotates about the origin of `curr` by the orientation difference, then moves to the interpolated position. Multiply on the left of a world matrix built from `curr`.
    inline glm::mat4 PoseInterpolationOffset(const glm::vec3& prevPosition, const glm::quat& prevRotation,
                                             const glm::vec3& currPosition, const glm::quat& currRotation, float alpha)
    {
        alpha = glm::clamp(alpha, 0.0f, 1.0f);
        const glm::vec3 position = glm::mix(prevPosition, currPosition, alpha);
        const glm::quat rotation = glm::slerp(prevRotation, currRotation, alpha);
        return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation * glm::inverse(currRotation)) *
               glm::translate(glm::mat4(1.0f), -currPosition);
    }
}

namespace Shard::Engine::Objects::Components
{

    struct STRUCT() SphereParams{
        FIELD(Editable, tooltip=Radius of the shape) float radius = 0.5f;
        SphereParams(float r) : radius(r) {}
        SphereParams() = default;
        ~SphereParams() = default;

        inline bool operator==(const SphereParams& other) const {
            const float epsilon = 1e-6f;
            return std::abs(radius - other.radius) < epsilon;
        }
    };


    struct STRUCT() CapsuleParams{
        FIELD(Editable, tooltip=Radius of the shape) float radius = 0.5f;
        FIELD(Editable, tooltip=Half of the height of the shape, without the caps for a capsule) float halfHeight = 0.5f;
        CapsuleParams(float r, float h) : radius(r), halfHeight(h) {}
        CapsuleParams() = default;
        ~CapsuleParams() = default;

        inline bool operator==(const CapsuleParams& other) const {
            const float epsilon = 1e-6f;
            return std::abs(radius - other.radius) < epsilon &&
                std::abs(halfHeight - other.halfHeight) < epsilon;
        }
    };

    struct STRUCT() BoxParams{
        FIELD(Editable, tooltip=Half of the size of the box along each axis) glm::vec3 halfExtent = glm::vec3(0.5f);
        BoxParams(glm::vec3 e) : halfExtent(e) {}
        BoxParams() = default;
        ~BoxParams() = default;

        inline bool operator==(const BoxParams& other) const {
            return halfExtent == other.halfExtent;
        }
    };

    struct STRUCT() CylinderParams{
        FIELD(Editable, tooltip=Radius of the shape) float radius = 0.5f;
        FIELD(Editable, tooltip=Half of the height of the shape, without the caps for a capsule) float halfHeight = 0.5f;
        CylinderParams(float r, float h) : radius(r), halfHeight(h) {}
        CylinderParams() = default;
        ~CylinderParams() = default;

        inline bool operator==(const CylinderParams& other) const {
            const float epsilon = 1e-6f;
            return std::abs(radius - other.radius) < epsilon &&
                std::abs(halfHeight - other.halfHeight) < epsilon;
        }
    };

    struct STRUCT() MeshParams{
        FIELD(Editable, tooltip=The mesh to collide with. Empty uses the model of this actor) Filesystem::AssetID mesh;
        FIELD(Editable, tooltip=Auto : exact triangles for a static or kinematic body and convex pieces for a dynamic one. Exact is only for bodies that do not move by physics) Physics::MeshApproximation approximation = Physics::MeshApproximation::Auto;
        FIELD(Editable, tooltip=Submeshes to use, for example 0 or 1-3 or 0,2. Empty uses all of them) std::string submeshes;
        FIELD(Editable, range=0.0f|0.5f, tooltip=Rounding of the convex pieces, in metres. A little helps stability) float convexRadius = 0.02f;
        FIELD(Editable, range=0.01f|1.0f, tooltip=Hulls only : how far a piece may stray from the mesh. Lower is tighter and slower to build) float concavity = 0.05f;
        FIELD(Editable, range=1|64, tooltip=Hulls only : most convex pieces) int maxHulls = 16;
        FIELD(Editable, range=8|256, tooltip=Most vertices of a convex piece) int maxHullVertices = 64;
        FIELD(Editable, range=0.0f|90.0f, tooltip=Exact only : edges flatter than this angle are smooth, so a body does not catch on the seams of a floor) float edgeAngle = 5.0f;
        FIELD(Editable, tooltip=Draws this collider in the viewport when Show Physics Shapes is on. Turn it off for a heavy mesh) bool showInViewport = true;
        MeshParams() = default;
        ~MeshParams() = default;

        inline bool operator==(const MeshParams& other) const {
            return mesh == other.mesh && approximation == other.approximation && submeshes == other.submeshes &&
                convexRadius == other.convexRadius && concavity == other.concavity && maxHulls == other.maxHulls &&
                maxHullVertices == other.maxHullVertices && edgeAngle == other.edgeAngle;
        }
    };

    struct PhysicsShapeEntry
    {
        Physics::PhysicsShape shapeType = Physics::PhysicsShape::BOX;
        InstancedStruct params;
        glm::vec3 offset = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    };

    class CLASS() PhysicsBody : public Component {

        public:
            PhysicsBody(std::shared_ptr<Actor> parent, uint32_t local_id);

            void CreateBody(EMotionType motionType);

            void ApplyTransformToPhysics(float dt);

            void SyncTransformFromPhysics();

            bool GetInterpolationOffset(float alpha, glm::mat4& offset) const;

            void Tick(float dt);

            void SetPosition(glm::vec3 newPos);
            void SetPositionD(const glm::dvec3& newPos);

            void SetRotation(glm::vec3 newRot);

            void SetRotation(glm::quat newRot);

            void Activate() override;

            void DeActivate() override;

            void Destroy() override
            {
                RemoveBody();
            }

            void RemoveBody(Collision::EndCause cause = Collision::EndCause::Destroyed);

            void RefreshCollision();

            bool BuildInspectorPatch(Collision::Patch& patch) const;

            void Deserialize(const json componentData) override;

            ordered_json Serialize() override;

            std::shared_ptr<Component> Clone() const override;

            size_t GetShapeCount() const { return shapes.size(); }

            Physics::PhysicsShape GetShapeType(size_t index = 0) const { return shapes.at(index).shapeType; }

            template<typename T>
            T& GetShapeParams(size_t index = 0);

            glm::vec3& GetShapeOffset(size_t index) { return shapes.at(index).offset; }
            glm::quat& GetShapeRotation(size_t index) { return shapes.at(index).rotation; }

            void AddShape(Physics::PhysicsShape type = Physics::PhysicsShape::BOX);

            void RemoveShape(size_t index);

            void SetShapeType(size_t index, Physics::PhysicsShape newType);

            void MarkShapesDirty() { shouldUpdateShape = true; }

            bool UsesMeshAsset(Filesystem::AssetID asset) const;

            bool GetMeshSettings(size_t shapeIndex, Physics::MeshColliderSettings& out) const;

            JPH::BodyID GetBodyID() const { return m_BodyID; }
            EMotionType GetMotionType() { return motionType; }

            void OnFieldChanged(const FieldChangedEvent& event) override;

            glm::vec3 GetPosition() const;
            glm::dvec3 GetPositionD() const;
            glm::quat GetRotation() const;
            glm::vec3 GetCenterOfMassPosition() const;

            void SetLinearVelocity(glm::vec3 velocity);
            glm::vec3 GetLinearVelocity() const;

            void SetAngularVelocity(glm::vec3 velocity);
            glm::vec3 GetAngularVelocity() const;

            void AddForce(glm::vec3 force);
            void AddForceAtPoint(glm::vec3 force, glm::vec3 point);
            void AddTorque(glm::vec3 torque);
            void AddImpulse(glm::vec3 impulse);
            void AddImpulseAtPoint(glm::vec3 impulse, glm::vec3 point);
            void AddAngularImpulse(glm::vec3 impulse);

            void SetMass(float newMass);
            float GetMass() const;

            void SetLinearDamping(float damping);
            float GetLinearDamping() const;

            void SetAngularDamping(float damping);
            float GetAngularDamping() const;

            void SetGravityFactor(float factor);
            float GetGravityFactor() const;

            void SetGravityMode(Physics::GravityMode mode);
            Physics::GravityMode GetGravityMode() const { return gravityMode; }

            void SetCustomGravity(glm::vec3 gravity);
            glm::vec3 GetCustomGravity() const { return customGravity; }

            glm::vec3 GetGravity() const;

            void SetFriction(float newFriction);
            float GetFriction() const;

            void SetRestitution(float newRestitution);
            float GetRestitution() const;

            FIELD(Editable, tooltip=Static never moves, Kinematic is moved by code only, Dynamic is driven by the physics simulation)
            EMotionType motionType = EMotionType::Static;

            FIELD(Editable, tooltip=Use the mass below instead of the one computed from the shape)
            bool overrideMass = false;

            FIELD(Editable, range=0.001f|100000.0f, tooltip=Mass in kg, only used when Override Mass is on)
            float mass = 1.0f;

            FIELD(Editable, range=0.0f|1.0f, tooltip=How fast the body loses its linear velocity, 0 keeps it forever)
            float linearDamping = 0.05f;

            FIELD(Editable, range=0.0f|1.0f, tooltip=How fast the body loses its spin, 0 keeps it forever)
            float angularDamping = 0.05f;

            FIELD(Editable, range=-10.0f|10.0f, tooltip=Multiplier on the gravity the body feels, 0 floats and a negative value falls upwards)
            float gravityFactor = 1.0f;

            FIELD(Editable, tooltip=Where the gravity comes from : Fields is the world gravity changed by the gravity fields around, World ignores the fields, Custom is the gravity below)
            Physics::GravityMode gravityMode = Physics::GravityMode::Fields;

            FIELD(Editable, tooltip=Custom mode only : the acceleration of the body in world units per second squared)
            glm::vec3 customGravity = glm::vec3(0.0f, -9.81f, 0.0f);

            FIELD(Editable, range=0.0f|1.0f, tooltip=Resistance when sliding on another body, 0 is ice and 1 is very grippy)
            float friction = 0.2f;

            FIELD(Editable, tooltip=Collision profile of this collider. Empty uses Default.Static or Default.Moving from the motion type)
            std::string collisionProfile;

            FIELD(Editable, tooltip=Replaces the mode of the profile, Inherit keeps it)
            Physics::CollisionModeChoice collisionMode = Physics::CollisionModeChoice::Inherit;

            FIELD(Editable, tooltip=Replaces the default response of the profile, Inherit keeps it)
            Physics::CollisionResponseChoice collisionDefault = Physics::CollisionResponseChoice::Inherit;

            FIELD(Editable, tooltip=Tag query for the colliders this one ignores, for example Enemy or Prop | Pickup)
            std::string collisionIgnore;

            FIELD(Editable, range=0.0f|1.0f, tooltip=Bounciness, 0 does not bounce and 1 bounces back with all its energy)
            float restitution = 0.0f;

        private:

            bool shouldUpdateShape = false;

            void RecordPose(const glm::vec3& position, const glm::quat& rotation);

            glm::vec3 m_PrevPosition = glm::vec3(0.0f);
            glm::quat m_PrevRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 m_CurrPosition = glm::vec3(0.0f);
            glm::quat m_CurrRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            uint64_t m_PoseStep = 0;
            bool m_PoseValid = false;

            JPH::ShapeRefC BuildShape(EMotionType motion);

            JPH::ShapeRefC CreateJoltShape(Physics::PhysicsShape shape, const InstancedStruct& params, EMotionType motion);

            void WatchMesh();

            bool FillMeshSettings(const MeshParams& p, Physics::MeshColliderSettings& out) const;

            Serialization::ReflectionContext ContextFor(Physics::PhysicsShape shape) const;

            bool m_MeshRefining = false;
            bool m_MeshMissing = false;
            uint64_t m_MeshGeneration = 0;

            void ApplyMassProperties();

            std::vector<PhysicsShapeEntry> shapes;

            JPH::BodyID m_BodyID = JPH::BodyID();

            JPH::ShapeRefC m_Shape;

            DECLARE_DESCRIPTOR(PhysicsBody)
    };

    template<typename T>
    T& PhysicsBody::GetShapeParams(size_t index)
    {
        return *static_cast<T*>(shapes.at(index).params.data);
    }
}

using namespace Shard::Engine;

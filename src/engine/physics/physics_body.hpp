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

#include "engine/world/components/component.hpp"

#include "engine/core/color.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/assets/reflection/attributes.hpp"


namespace Shard::Engine::Physics
{
    /// @brief The world-space matrix that carries something sitting at pose `curr` to the pose `alpha` of the way from `prev`
    /// to `curr` : it turns it about the origin of `curr` by what separates the two orientations, then moves it to the
    /// interpolated position. Multiplied on the left of a world matrix that was built from `curr`, it keeps the scale and the parents.
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
        FIELD(Editable) float radius = 0.5f;
        SphereParams(float r) : radius(r) {}
        SphereParams() = default;
        ~SphereParams() = default;

        inline bool operator==(const SphereParams& other) const {
            const float epsilon = 1e-6f;
            return std::abs(radius - other.radius) < epsilon;
        }
    };


    struct STRUCT() CapsuleParams{
        FIELD(Editable) float radius = 0.5f;
        FIELD(Editable) float halfHeight = 0.5f;
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
        FIELD(Editable) glm::vec3 halfExtent = glm::vec3(0.5f);
        BoxParams(glm::vec3 e) : halfExtent(e) {}
        BoxParams() = default;
        ~BoxParams() = default;

        inline bool operator==(const BoxParams& other) const {
            return halfExtent == other.halfExtent;
        }
    };

    struct STRUCT() CylinderParams{
        FIELD(Editable) float radius = 0.5f;
        FIELD(Editable) float halfHeight = 0.5f;
        CylinderParams(float r, float h) : radius(r), halfHeight(h) {}
        CylinderParams() = default;
        ~CylinderParams() = default;

        inline bool operator==(const CylinderParams& other) const {
            const float epsilon = 1e-6f;
            return std::abs(radius - other.radius) < epsilon &&
                std::abs(halfHeight - other.halfHeight) < epsilon;
        }
    };

    /// @brief A single collision shape belonging to a PhysicsBody, with its own local offset/rotation.
    /// @note Not exposed through the FIELD()/STRUCT() reflection macros - the editor draws the shape
    /// list with a dedicated custom UI (see PropertiesPanel) instead of the generic reflected-field one,
    /// since the generic system doesn't support editing a vector of structs.
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

            /// @brief Where a dynamic body is drawn between its last two simulated poses, as a world-space matrix that
            /// moves the body from the pose its transform holds (the last step) to the interpolated one.
            /// @param alpha how far the frame is between the last step and the next one, in [0, 1) (TimeManager::GetFixedStepAlpha)
            /// @return false when there is nothing to correct (not dynamic, no step simulated yet, or the body did not move)
            bool GetInterpolationOffset(float alpha, glm::mat4& offset) const;

            void Tick(float dt);

            void SetPosition(glm::vec3 newPos);

            void SetRotation(glm::vec3 newRot);

            void SetRotation(glm::quat newRot);

            void Activate() override;

            void DeActivate() override;

            void Destroy() override
            {
                RemoveBody();
            }

            void RemoveBody();

            void Deserialize(const json componentData) override;

            ordered_json Serialize() override;

            std::shared_ptr<Component> Clone() const override;

            // ---------------- Shape list ----------------

            size_t GetShapeCount() const { return shapes.size(); }

            Physics::PhysicsShape GetShapeType(size_t index = 0) const { return shapes.at(index).shapeType; }

            template<typename T>
            T& GetShapeParams(size_t index = 0);

            glm::vec3& GetShapeOffset(size_t index) { return shapes.at(index).offset; }
            glm::quat& GetShapeRotation(size_t index) { return shapes.at(index).rotation; }

            /// @brief Appends a new shape entry of the given type (with default params) to the body
            void AddShape(Physics::PhysicsShape type = Physics::PhysicsShape::BOX);

            /// @brief Removes the shape entry at the given index. A body always keeps at least one shape.
            void RemoveShape(size_t index);

            /// @brief Changes the shape type of an existing entry, reinitializing its params for the new type
            void SetShapeType(size_t index, Physics::PhysicsShape newType);

            /// @brief Call after mutating a shape entry's params/offset/rotation in place (e.g. from the editor)
            /// to schedule a rebuild of the underlying Jolt shape on the next Tick().
            void MarkShapesDirty() { shouldUpdateShape = true; }

            JPH::BodyID GetBodyID() const { return m_BodyID; }
            EMotionType GetMotionType() { return motionType; }

            void OnFieldChanged(const FieldChangedEvent& event) override;

            // ---------------- Runtime transform / motion API ----------------

            glm::vec3 GetPosition() const;
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

            void SetFriction(float newFriction);
            float GetFriction() const;

            void SetRestitution(float newRestitution);
            float GetRestitution() const;

            FIELD(Editable)
            EMotionType motionType = EMotionType::Static;

            FIELD(Editable)
            bool overrideMass = false;

            FIELD(Editable, range=0.001f|100000.0f)
            float mass = 1.0f;

            FIELD(Editable, range=0.0f|1.0f)
            float linearDamping = 0.05f;

            FIELD(Editable, range=0.0f|1.0f)
            float angularDamping = 0.05f;

            FIELD(Editable, range=-10.0f|10.0f)
            float gravityFactor = 1.0f;

            FIELD(Editable, range=0.0f|1.0f)
            float friction = 0.2f;

            FIELD(Editable, range=0.0f|1.0f)
            float restitution = 0.0f;

        private:

            bool shouldUpdateShape = false;

            /// Keeps the last two poses Jolt gave (world space), shifting only when a new step was simulated since the last read
            void RecordPose(const glm::vec3& position, const glm::quat& rotation);

            glm::vec3 m_PrevPosition = glm::vec3(0.0f);
            glm::quat m_PrevRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 m_CurrPosition = glm::vec3(0.0f);
            glm::quat m_CurrRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            uint64_t m_PoseStep = 0;            // PhysicsManager::GetStepCount() when the current pose was recorded
            bool m_PoseValid = false;           // false after a teleport / a rebuild : the next pose is taken as both previous and current

            JPH::ShapeRefC BuildShape();

            JPH::ShapeRefC CreateJoltShape(Physics::PhysicsShape shape, const InstancedStruct& params);

            /// @brief Pushes the current overrideMass/mass fields to the live body (or resets to the
            /// shape's auto-calculated mass when overrideMass is false). Used by both SetMass() and
            /// OnFieldChanged() so toggling "overrideMass" off doesn't get forced back on.
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

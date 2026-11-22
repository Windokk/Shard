#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>

#include <memory>

#include <glm/glm.hpp>

#include "engine/core/tags/tag_query.hpp"
#include "engine/world/components/component.hpp"

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/assets/reflection/attributes.hpp"

namespace Shard::Engine::Physics
{
    class PhysicsManager;

    enum class ConstraintMotor : int {
        Off = 0,
        Velocity,
        Position
    };

    enum class SwingShape : int {
        Cone = 0,
        Pyramid
    };
}

namespace Shard::Engine::Objects::Components
{
    class CLASS() PhysicsConstraint : public Component {
        public:
            PhysicsConstraint(std::shared_ptr<Actor> parent, uint32_t local_id);
            PhysicsConstraint(const PhysicsConstraint& other) = default;
            ~PhysicsConstraint() override;

            void Activate() override;
            void DeActivate() override;
            void Destroy() override;
            void OnFieldChanged(const FieldChangedEvent& event) override;

            bool Sync(Physics::PhysicsManager& physics);
            void Release();
            void OnConstraintDropped();
            void CheckBreak(float deltaTime);

            bool IsBuilt() const { return m_Runtime.live != nullptr; }
            bool IsBroken() const { return m_Runtime.broken; }
            void Break();
            void Repair();

            void ConnectTo(const std::shared_ptr<Actor>& actor);
            void ConnectToWorld();
            void ConnectByTag();
            std::shared_ptr<Actor> GetTarget() const;

            FIELD(Editable, tooltip=Tag query of the actor this one is connected to, for example Door.Frame. The first match with a physics body is used. Empty connects to the world, so the actor hangs in place)
            Core::TagQuery target;

            FIELD(Editable, tooltip=Where the constraint pivots, in the space of this actor)
            glm::vec3 anchor = glm::vec3(0.0f);

            FIELD(Editable, tooltip=Off lets the two connected bodies collide with each other)
            bool collideConnected = false;

            FIELD(Editable, range=0.0f|1000000000.0f, tooltip=Force in newtons above which the constraint breaks. 0 never breaks)
            float breakForce = 0.0f;

            FIELD(Editable, range=0.0f|1000000000.0f, tooltip=Torque in newton metres above which the constraint breaks. 0 never breaks)
            float breakTorque = 0.0f;

        protected:
            struct Frame {
                JPH::RVec3 pivot;
                JPH::Quat rotation;
                JPH::Vec3 scale;
            };

            virtual JPH::TwoBodyConstraint* Create(const Frame& frame, JPH::Body& a, JPH::Body& b) = 0;
            virtual void Configure(JPH::TwoBodyConstraint& constraint) {}
            virtual void Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float& angular) const {}

            JPH::TwoBodyConstraint* Live() const { return m_Runtime.live; }
            void Reconfigure();

            struct Runtime {
                Runtime() = default;
                Runtime(const Runtime&) {}
                Runtime& operator=(const Runtime&) { return *this; }

                Physics::PhysicsManager* manager = nullptr;
                JPH::TwoBodyConstraint* live = nullptr;
                JPH::BodyID a;
                JPH::BodyID b;
                bool dirty = true;
                bool broken = false;
                bool ignored = false;
                bool explicitWorld = false;
                int cooldown = 0;
                std::weak_ptr<Actor> explicitTarget;
                std::weak_ptr<Actor> found;
            };

            Runtime m_Runtime;

        private:
            bool ResolveTarget(JPH::BodyID& body);
            bool Build(Physics::PhysicsManager& physics, JPH::BodyID a, JPH::BodyID b);
    };

    class CLASS() FixedConstraint : public PhysicsConstraint {
        public:
            FixedConstraint(std::shared_ptr<Actor> parent, uint32_t local_id);

            ordered_json Serialize() override;
            std::shared_ptr<Component> Clone() const override;

            DECLARE_DESCRIPTOR(FixedConstraint)

        protected:
            JPH::TwoBodyConstraint* Create(const Frame& frame, JPH::Body& a, JPH::Body& b) override;
            void Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float& angular) const override;
    };

    class CLASS() PointConstraint : public PhysicsConstraint {
        public:
            PointConstraint(std::shared_ptr<Actor> parent, uint32_t local_id);

            ordered_json Serialize() override;
            std::shared_ptr<Component> Clone() const override;

            DECLARE_DESCRIPTOR(PointConstraint)

        protected:
            JPH::TwoBodyConstraint* Create(const Frame& frame, JPH::Body& a, JPH::Body& b) override;
            void Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float& angular) const override;
    };

    class CLASS() HingeConstraint : public PhysicsConstraint {
        public:
            HingeConstraint(std::shared_ptr<Actor> parent, uint32_t local_id);

            ordered_json Serialize() override;
            std::shared_ptr<Component> Clone() const override;

            void SetMotor(Physics::ConstraintMotor mode, float targetValue);
            float GetAngle() const;

            FIELD(Editable, tooltip=Axis the hinge turns around, in the space of this actor)
            glm::vec3 axis = glm::vec3(0.0f, 1.0f, 0.0f);

            FIELD(Editable, tooltip=Limits the turn to the angles below, measured from the pose the two bodies have when the constraint starts)
            bool limited = false;

            FIELD(Editable, range=-180.0f|0.0f, tooltip=Lowest angle in degrees)
            float minAngle = -90.0f;

            FIELD(Editable, range=0.0f|180.0f, tooltip=Highest angle in degrees)
            float maxAngle = 90.0f;

            FIELD(Editable, range=0.0f|100000.0f, tooltip=Torque in newton metres that resists the turn when no motor drives it)
            float friction = 0.0f;

            FIELD(Editable, range=0.0f|1000.0f, tooltip=Makes the limits soft with a spring of this frequency in hertz. 0 keeps them hard)
            float limitFrequency = 0.0f;

            FIELD(Editable, range=0.0f|10.0f, tooltip=Damping ratio of the soft limits)
            float limitDamping = 1.0f;

            FIELD(Editable, tooltip=Velocity turns at the target speed, Position turns to the target angle)
            Physics::ConstraintMotor motor = Physics::ConstraintMotor::Off;

            FIELD(Editable, tooltip=Velocity motor : degrees per second. Position motor : degrees)
            float motorTarget = 0.0f;

            FIELD(Editable, range=0.0f|1000000000.0f, tooltip=Most torque in newton metres the motor can apply)
            float motorMaxTorque = 1000.0f;

            FIELD(Editable, range=0.0f|1000.0f, tooltip=Position motor : how stiff it is, as a frequency in hertz)
            float motorFrequency = 2.0f;

            FIELD(Editable, range=0.0f|10.0f, tooltip=Position motor : damping ratio)
            float motorDamping = 1.0f;

            DECLARE_DESCRIPTOR(HingeConstraint)

        protected:
            JPH::TwoBodyConstraint* Create(const Frame& frame, JPH::Body& a, JPH::Body& b) override;
            void Configure(JPH::TwoBodyConstraint& constraint) override;
            void Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float& angular) const override;
    };

    class CLASS() SliderConstraint : public PhysicsConstraint {
        public:
            SliderConstraint(std::shared_ptr<Actor> parent, uint32_t local_id);

            ordered_json Serialize() override;
            std::shared_ptr<Component> Clone() const override;

            void SetMotor(Physics::ConstraintMotor mode, float targetValue);
            float GetPosition() const;

            FIELD(Editable, tooltip=Axis the actor slides along, in the space of this actor)
            glm::vec3 axis = glm::vec3(1.0f, 0.0f, 0.0f);

            FIELD(Editable, tooltip=Limits the travel to the distances below, measured from the pose the two bodies have when the constraint starts)
            bool limited = false;

            FIELD(Editable, range=-1000000.0f|0.0f, tooltip=Lowest position)
            float minLimit = -1.0f;

            FIELD(Editable, range=0.0f|1000000.0f, tooltip=Highest position)
            float maxLimit = 1.0f;

            FIELD(Editable, range=0.0f|1000000000.0f, tooltip=Force in newtons that resists the slide when no motor drives it)
            float friction = 0.0f;

            FIELD(Editable, range=0.0f|1000.0f, tooltip=Makes the limits soft with a spring of this frequency in hertz. 0 keeps them hard)
            float limitFrequency = 0.0f;

            FIELD(Editable, range=0.0f|10.0f, tooltip=Damping ratio of the soft limits)
            float limitDamping = 1.0f;

            FIELD(Editable, tooltip=Velocity slides at the target speed, Position slides to the target position)
            Physics::ConstraintMotor motor = Physics::ConstraintMotor::Off;

            FIELD(Editable, tooltip=Velocity motor : units per second. Position motor : position)
            float motorTarget = 0.0f;

            FIELD(Editable, range=0.0f|1000000000.0f, tooltip=Most force in newtons the motor can apply)
            float motorMaxForce = 1000.0f;

            FIELD(Editable, range=0.0f|1000.0f, tooltip=Position motor : how stiff it is, as a frequency in hertz)
            float motorFrequency = 2.0f;

            FIELD(Editable, range=0.0f|10.0f, tooltip=Position motor : damping ratio)
            float motorDamping = 1.0f;

            DECLARE_DESCRIPTOR(SliderConstraint)

        protected:
            JPH::TwoBodyConstraint* Create(const Frame& frame, JPH::Body& a, JPH::Body& b) override;
            void Configure(JPH::TwoBodyConstraint& constraint) override;
            void Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float& angular) const override;
    };

    class CLASS() DistanceConstraint : public PhysicsConstraint {
        public:
            DistanceConstraint(std::shared_ptr<Actor> parent, uint32_t local_id);

            ordered_json Serialize() override;
            std::shared_ptr<Component> Clone() const override;

            FIELD(Editable, tooltip=Where the other end is attached, in the space of the connected actor. In the world when there is none)
            glm::vec3 targetAnchor = glm::vec3(0.0f);

            FIELD(Editable, range=-1.0f|1000000.0f, tooltip=Closest the two ends can come. A negative value keeps the distance they have when the constraint starts)
            float minDistance = -1.0f;

            FIELD(Editable, range=-1.0f|1000000.0f, tooltip=Farthest the two ends can go, like the length of a rope. A negative value keeps the distance they have when the constraint starts)
            float maxDistance = -1.0f;

            FIELD(Editable, range=0.0f|1000.0f, tooltip=Makes the constraint a spring of this frequency in hertz. 0 keeps it rigid)
            float springFrequency = 0.0f;

            FIELD(Editable, range=0.0f|10.0f, tooltip=Damping ratio of the spring)
            float springDamping = 0.5f;

            DECLARE_DESCRIPTOR(DistanceConstraint)

        protected:
            JPH::TwoBodyConstraint* Create(const Frame& frame, JPH::Body& a, JPH::Body& b) override;
            void Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float& angular) const override;
    };

    class CLASS() ConeConstraint : public PhysicsConstraint {
        public:
            ConeConstraint(std::shared_ptr<Actor> parent, uint32_t local_id);

            ordered_json Serialize() override;
            std::shared_ptr<Component> Clone() const override;

            FIELD(Editable, tooltip=Axis at the center of the cone, in the space of this actor)
            glm::vec3 axis = glm::vec3(0.0f, 1.0f, 0.0f);

            FIELD(Editable, range=0.0f|180.0f, tooltip=Angle in degrees the axis can lean away from where it started)
            float coneAngle = 30.0f;

            DECLARE_DESCRIPTOR(ConeConstraint)

        protected:
            JPH::TwoBodyConstraint* Create(const Frame& frame, JPH::Body& a, JPH::Body& b) override;
            void Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float& angular) const override;
    };

    class CLASS() SwingTwistConstraint : public PhysicsConstraint {
        public:
            SwingTwistConstraint(std::shared_ptr<Actor> parent, uint32_t local_id);

            ordered_json Serialize() override;
            std::shared_ptr<Component> Clone() const override;

            FIELD(Editable, tooltip=Axis of the twist, along the bone for a limb, in the space of this actor)
            glm::vec3 twistAxis = glm::vec3(1.0f, 0.0f, 0.0f);

            FIELD(Editable, tooltip=Cone is a round range of swing, Pyramid a square one)
            Physics::SwingShape swingShape = Physics::SwingShape::Cone;

            FIELD(Editable, range=0.0f|180.0f, tooltip=Swing in degrees away from the twist axis, in one direction)
            float swingAngle = 45.0f;

            FIELD(Editable, range=0.0f|180.0f, tooltip=Swing in degrees away from the twist axis, in the perpendicular direction)
            float planeAngle = 45.0f;

            FIELD(Editable, range=-180.0f|180.0f, tooltip=Lowest twist in degrees)
            float twistMin = -45.0f;

            FIELD(Editable, range=-180.0f|180.0f, tooltip=Highest twist in degrees)
            float twistMax = 45.0f;

            FIELD(Editable, range=0.0f|100000.0f, tooltip=Torque in newton metres that resists the movement)
            float friction = 0.0f;

            DECLARE_DESCRIPTOR(SwingTwistConstraint)

        protected:
            JPH::TwoBodyConstraint* Create(const Frame& frame, JPH::Body& a, JPH::Body& b) override;
            void Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float& angular) const override;
    };
}

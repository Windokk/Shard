#pragma once

#include "physics_constraint.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

// Reflection for class FixedConstraint

inline FieldInfo FixedConstraint_target_info = {
    "target",
    TypeID::TagQuery,
    offsetof(Shard::Engine::Objects::Components::FixedConstraint, target),
    Editable,
    0, 0,
    "Tag query of the actor this one is connected to, for example Door.Frame. The first match with a physics body is used. Empty connects to the world, so the actor hangs in place",
    nullptr,
    nullptr,
    &CopyConstruct<Core::TagQuery>,
    &Assign<Core::TagQuery>,
    &Destroy<Core::TagQuery>,
    &Equals<Core::TagQuery>
};

inline FieldInfo FixedConstraint_anchor_info = {
    "anchor",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::FixedConstraint, anchor),
    Editable,
    0, 0,
    "Where the constraint pivots, in the space of this actor",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo FixedConstraint_collideConnected_info = {
    "collideConnected",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::FixedConstraint, collideConnected),
    Editable,
    0, 0,
    "Off lets the two connected bodies collide with each other",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo FixedConstraint_breakForce_info = {
    "breakForce",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::FixedConstraint, breakForce),
    Editable,
    0.0f, 1000000000.0f,
    "Force in newtons above which the constraint breaks. 0 never breaks",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo FixedConstraint_breakTorque_info = {
    "breakTorque",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::FixedConstraint, breakTorque),
    Editable,
    0.0f, 1000000000.0f,
    "Torque in newton metres above which the constraint breaks. 0 never breaks",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::FixedConstraint::descriptor = {
    "FixedConstraint",
    {
        &FixedConstraint_target_info,
        &FixedConstraint_anchor_info,
        &FixedConstraint_collideConnected_info,
        &FixedConstraint_breakForce_info,
        &FixedConstraint_breakTorque_info,
    }
};

// Reflection for class PointConstraint

inline FieldInfo PointConstraint_target_info = {
    "target",
    TypeID::TagQuery,
    offsetof(Shard::Engine::Objects::Components::PointConstraint, target),
    Editable,
    0, 0,
    "Tag query of the actor this one is connected to, for example Door.Frame. The first match with a physics body is used. Empty connects to the world, so the actor hangs in place",
    nullptr,
    nullptr,
    &CopyConstruct<Core::TagQuery>,
    &Assign<Core::TagQuery>,
    &Destroy<Core::TagQuery>,
    &Equals<Core::TagQuery>
};

inline FieldInfo PointConstraint_anchor_info = {
    "anchor",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::PointConstraint, anchor),
    Editable,
    0, 0,
    "Where the constraint pivots, in the space of this actor",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo PointConstraint_collideConnected_info = {
    "collideConnected",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::PointConstraint, collideConnected),
    Editable,
    0, 0,
    "Off lets the two connected bodies collide with each other",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo PointConstraint_breakForce_info = {
    "breakForce",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::PointConstraint, breakForce),
    Editable,
    0.0f, 1000000000.0f,
    "Force in newtons above which the constraint breaks. 0 never breaks",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo PointConstraint_breakTorque_info = {
    "breakTorque",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::PointConstraint, breakTorque),
    Editable,
    0.0f, 1000000000.0f,
    "Torque in newton metres above which the constraint breaks. 0 never breaks",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::PointConstraint::descriptor = {
    "PointConstraint",
    {
        &PointConstraint_target_info,
        &PointConstraint_anchor_info,
        &PointConstraint_collideConnected_info,
        &PointConstraint_breakForce_info,
        &PointConstraint_breakTorque_info,
    }
};

// Reflection for class HingeConstraint

inline FieldInfo HingeConstraint_target_info = {
    "target",
    TypeID::TagQuery,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, target),
    Editable,
    0, 0,
    "Tag query of the actor this one is connected to, for example Door.Frame. The first match with a physics body is used. Empty connects to the world, so the actor hangs in place",
    nullptr,
    nullptr,
    &CopyConstruct<Core::TagQuery>,
    &Assign<Core::TagQuery>,
    &Destroy<Core::TagQuery>,
    &Equals<Core::TagQuery>
};

inline FieldInfo HingeConstraint_anchor_info = {
    "anchor",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, anchor),
    Editable,
    0, 0,
    "Where the constraint pivots, in the space of this actor",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo HingeConstraint_collideConnected_info = {
    "collideConnected",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, collideConnected),
    Editable,
    0, 0,
    "Off lets the two connected bodies collide with each other",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo HingeConstraint_breakForce_info = {
    "breakForce",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, breakForce),
    Editable,
    0.0f, 1000000000.0f,
    "Force in newtons above which the constraint breaks. 0 never breaks",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo HingeConstraint_breakTorque_info = {
    "breakTorque",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, breakTorque),
    Editable,
    0.0f, 1000000000.0f,
    "Torque in newton metres above which the constraint breaks. 0 never breaks",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo HingeConstraint_axis_info = {
    "axis",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, axis),
    Editable,
    0, 0,
    "Axis the hinge turns around, in the space of this actor",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo HingeConstraint_limited_info = {
    "limited",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, limited),
    Editable,
    0, 0,
    "Limits the turn to the angles below, measured from the pose the two bodies have when the constraint starts",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo HingeConstraint_minAngle_info = {
    "minAngle",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, minAngle),
    Editable,
    -180.0f, 0.0f,
    "Lowest angle in degrees",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo HingeConstraint_maxAngle_info = {
    "maxAngle",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, maxAngle),
    Editable,
    0.0f, 180.0f,
    "Highest angle in degrees",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo HingeConstraint_friction_info = {
    "friction",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, friction),
    Editable,
    0.0f, 100000.0f,
    "Torque in newton metres that resists the turn when no motor drives it",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo HingeConstraint_limitFrequency_info = {
    "limitFrequency",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, limitFrequency),
    Editable,
    0.0f, 1000.0f,
    "Makes the limits soft with a spring of this frequency in hertz. 0 keeps them hard",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo HingeConstraint_limitDamping_info = {
    "limitDamping",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, limitDamping),
    Editable,
    0.0f, 10.0f,
    "Damping ratio of the soft limits",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

static EnumDescriptor ConstraintMotor_descriptor = {
    "ConstraintMotor",
    {
        { 0, "Off" },
        { 1, "Velocity" },
        { 2, "Position" },
    },
    sizeof(Physics::ConstraintMotor)
};

inline FieldInfo HingeConstraint_motor_info = {
    "motor",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, motor),
    Editable,
    0, 0,
    "Velocity turns at the target speed, Position turns to the target angle",
    nullptr,
    &ConstraintMotor_descriptor,
    &CopyConstruct<Physics::ConstraintMotor>,
    &Assign<Physics::ConstraintMotor>,
    &Destroy<Physics::ConstraintMotor>,
    &Equals<Physics::ConstraintMotor>
};

inline FieldInfo HingeConstraint_motorTarget_info = {
    "motorTarget",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, motorTarget),
    Editable,
    0, 0,
    "Velocity motor : degrees per second. Position motor : degrees",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo HingeConstraint_motorMaxTorque_info = {
    "motorMaxTorque",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, motorMaxTorque),
    Editable,
    0.0f, 1000000000.0f,
    "Most torque in newton metres the motor can apply",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo HingeConstraint_motorFrequency_info = {
    "motorFrequency",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, motorFrequency),
    Editable,
    0.0f, 1000.0f,
    "Position motor : how stiff it is, as a frequency in hertz",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo HingeConstraint_motorDamping_info = {
    "motorDamping",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::HingeConstraint, motorDamping),
    Editable,
    0.0f, 10.0f,
    "Position motor : damping ratio",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::HingeConstraint::descriptor = {
    "HingeConstraint",
    {
        &HingeConstraint_target_info,
        &HingeConstraint_anchor_info,
        &HingeConstraint_collideConnected_info,
        &HingeConstraint_breakForce_info,
        &HingeConstraint_breakTorque_info,
        &HingeConstraint_axis_info,
        &HingeConstraint_limited_info,
        &HingeConstraint_minAngle_info,
        &HingeConstraint_maxAngle_info,
        &HingeConstraint_friction_info,
        &HingeConstraint_limitFrequency_info,
        &HingeConstraint_limitDamping_info,
        &HingeConstraint_motor_info,
        &HingeConstraint_motorTarget_info,
        &HingeConstraint_motorMaxTorque_info,
        &HingeConstraint_motorFrequency_info,
        &HingeConstraint_motorDamping_info,
    }
};

// Reflection for class SliderConstraint

inline FieldInfo SliderConstraint_target_info = {
    "target",
    TypeID::TagQuery,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, target),
    Editable,
    0, 0,
    "Tag query of the actor this one is connected to, for example Door.Frame. The first match with a physics body is used. Empty connects to the world, so the actor hangs in place",
    nullptr,
    nullptr,
    &CopyConstruct<Core::TagQuery>,
    &Assign<Core::TagQuery>,
    &Destroy<Core::TagQuery>,
    &Equals<Core::TagQuery>
};

inline FieldInfo SliderConstraint_anchor_info = {
    "anchor",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, anchor),
    Editable,
    0, 0,
    "Where the constraint pivots, in the space of this actor",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo SliderConstraint_collideConnected_info = {
    "collideConnected",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, collideConnected),
    Editable,
    0, 0,
    "Off lets the two connected bodies collide with each other",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo SliderConstraint_breakForce_info = {
    "breakForce",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, breakForce),
    Editable,
    0.0f, 1000000000.0f,
    "Force in newtons above which the constraint breaks. 0 never breaks",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SliderConstraint_breakTorque_info = {
    "breakTorque",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, breakTorque),
    Editable,
    0.0f, 1000000000.0f,
    "Torque in newton metres above which the constraint breaks. 0 never breaks",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SliderConstraint_axis_info = {
    "axis",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, axis),
    Editable,
    0, 0,
    "Axis the actor slides along, in the space of this actor",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo SliderConstraint_limited_info = {
    "limited",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, limited),
    Editable,
    0, 0,
    "Limits the travel to the distances below, measured from the pose the two bodies have when the constraint starts",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo SliderConstraint_minLimit_info = {
    "minLimit",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, minLimit),
    Editable,
    -1000000.0f, 0.0f,
    "Lowest position",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SliderConstraint_maxLimit_info = {
    "maxLimit",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, maxLimit),
    Editable,
    0.0f, 1000000.0f,
    "Highest position",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SliderConstraint_friction_info = {
    "friction",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, friction),
    Editable,
    0.0f, 1000000000.0f,
    "Force in newtons that resists the slide when no motor drives it",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SliderConstraint_limitFrequency_info = {
    "limitFrequency",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, limitFrequency),
    Editable,
    0.0f, 1000.0f,
    "Makes the limits soft with a spring of this frequency in hertz. 0 keeps them hard",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SliderConstraint_limitDamping_info = {
    "limitDamping",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, limitDamping),
    Editable,
    0.0f, 10.0f,
    "Damping ratio of the soft limits",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SliderConstraint_motor_info = {
    "motor",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, motor),
    Editable,
    0, 0,
    "Velocity slides at the target speed, Position slides to the target position",
    nullptr,
    &ConstraintMotor_descriptor,
    &CopyConstruct<Physics::ConstraintMotor>,
    &Assign<Physics::ConstraintMotor>,
    &Destroy<Physics::ConstraintMotor>,
    &Equals<Physics::ConstraintMotor>
};

inline FieldInfo SliderConstraint_motorTarget_info = {
    "motorTarget",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, motorTarget),
    Editable,
    0, 0,
    "Velocity motor : units per second. Position motor : position",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SliderConstraint_motorMaxForce_info = {
    "motorMaxForce",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, motorMaxForce),
    Editable,
    0.0f, 1000000000.0f,
    "Most force in newtons the motor can apply",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SliderConstraint_motorFrequency_info = {
    "motorFrequency",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, motorFrequency),
    Editable,
    0.0f, 1000.0f,
    "Position motor : how stiff it is, as a frequency in hertz",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SliderConstraint_motorDamping_info = {
    "motorDamping",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SliderConstraint, motorDamping),
    Editable,
    0.0f, 10.0f,
    "Position motor : damping ratio",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::SliderConstraint::descriptor = {
    "SliderConstraint",
    {
        &SliderConstraint_target_info,
        &SliderConstraint_anchor_info,
        &SliderConstraint_collideConnected_info,
        &SliderConstraint_breakForce_info,
        &SliderConstraint_breakTorque_info,
        &SliderConstraint_axis_info,
        &SliderConstraint_limited_info,
        &SliderConstraint_minLimit_info,
        &SliderConstraint_maxLimit_info,
        &SliderConstraint_friction_info,
        &SliderConstraint_limitFrequency_info,
        &SliderConstraint_limitDamping_info,
        &SliderConstraint_motor_info,
        &SliderConstraint_motorTarget_info,
        &SliderConstraint_motorMaxForce_info,
        &SliderConstraint_motorFrequency_info,
        &SliderConstraint_motorDamping_info,
    }
};

// Reflection for class DistanceConstraint

inline FieldInfo DistanceConstraint_target_info = {
    "target",
    TypeID::TagQuery,
    offsetof(Shard::Engine::Objects::Components::DistanceConstraint, target),
    Editable,
    0, 0,
    "Tag query of the actor this one is connected to, for example Door.Frame. The first match with a physics body is used. Empty connects to the world, so the actor hangs in place",
    nullptr,
    nullptr,
    &CopyConstruct<Core::TagQuery>,
    &Assign<Core::TagQuery>,
    &Destroy<Core::TagQuery>,
    &Equals<Core::TagQuery>
};

inline FieldInfo DistanceConstraint_anchor_info = {
    "anchor",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::DistanceConstraint, anchor),
    Editable,
    0, 0,
    "Where the constraint pivots, in the space of this actor",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo DistanceConstraint_collideConnected_info = {
    "collideConnected",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::DistanceConstraint, collideConnected),
    Editable,
    0, 0,
    "Off lets the two connected bodies collide with each other",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo DistanceConstraint_breakForce_info = {
    "breakForce",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::DistanceConstraint, breakForce),
    Editable,
    0.0f, 1000000000.0f,
    "Force in newtons above which the constraint breaks. 0 never breaks",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo DistanceConstraint_breakTorque_info = {
    "breakTorque",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::DistanceConstraint, breakTorque),
    Editable,
    0.0f, 1000000000.0f,
    "Torque in newton metres above which the constraint breaks. 0 never breaks",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo DistanceConstraint_targetAnchor_info = {
    "targetAnchor",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::DistanceConstraint, targetAnchor),
    Editable,
    0, 0,
    "Where the other end is attached, in the space of the connected actor. In the world when there is none",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo DistanceConstraint_minDistance_info = {
    "minDistance",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::DistanceConstraint, minDistance),
    Editable,
    -1.0f, 1000000.0f,
    "Closest the two ends can come. A negative value keeps the distance they have when the constraint starts",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo DistanceConstraint_maxDistance_info = {
    "maxDistance",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::DistanceConstraint, maxDistance),
    Editable,
    -1.0f, 1000000.0f,
    "Farthest the two ends can go, like the length of a rope. A negative value keeps the distance they have when the constraint starts",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo DistanceConstraint_springFrequency_info = {
    "springFrequency",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::DistanceConstraint, springFrequency),
    Editable,
    0.0f, 1000.0f,
    "Makes the constraint a spring of this frequency in hertz. 0 keeps it rigid",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo DistanceConstraint_springDamping_info = {
    "springDamping",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::DistanceConstraint, springDamping),
    Editable,
    0.0f, 10.0f,
    "Damping ratio of the spring",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::DistanceConstraint::descriptor = {
    "DistanceConstraint",
    {
        &DistanceConstraint_target_info,
        &DistanceConstraint_anchor_info,
        &DistanceConstraint_collideConnected_info,
        &DistanceConstraint_breakForce_info,
        &DistanceConstraint_breakTorque_info,
        &DistanceConstraint_targetAnchor_info,
        &DistanceConstraint_minDistance_info,
        &DistanceConstraint_maxDistance_info,
        &DistanceConstraint_springFrequency_info,
        &DistanceConstraint_springDamping_info,
    }
};

// Reflection for class ConeConstraint

inline FieldInfo ConeConstraint_target_info = {
    "target",
    TypeID::TagQuery,
    offsetof(Shard::Engine::Objects::Components::ConeConstraint, target),
    Editable,
    0, 0,
    "Tag query of the actor this one is connected to, for example Door.Frame. The first match with a physics body is used. Empty connects to the world, so the actor hangs in place",
    nullptr,
    nullptr,
    &CopyConstruct<Core::TagQuery>,
    &Assign<Core::TagQuery>,
    &Destroy<Core::TagQuery>,
    &Equals<Core::TagQuery>
};

inline FieldInfo ConeConstraint_anchor_info = {
    "anchor",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::ConeConstraint, anchor),
    Editable,
    0, 0,
    "Where the constraint pivots, in the space of this actor",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo ConeConstraint_collideConnected_info = {
    "collideConnected",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::ConeConstraint, collideConnected),
    Editable,
    0, 0,
    "Off lets the two connected bodies collide with each other",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo ConeConstraint_breakForce_info = {
    "breakForce",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ConeConstraint, breakForce),
    Editable,
    0.0f, 1000000000.0f,
    "Force in newtons above which the constraint breaks. 0 never breaks",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo ConeConstraint_breakTorque_info = {
    "breakTorque",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ConeConstraint, breakTorque),
    Editable,
    0.0f, 1000000000.0f,
    "Torque in newton metres above which the constraint breaks. 0 never breaks",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo ConeConstraint_axis_info = {
    "axis",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::ConeConstraint, axis),
    Editable,
    0, 0,
    "Axis at the center of the cone, in the space of this actor",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo ConeConstraint_coneAngle_info = {
    "coneAngle",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ConeConstraint, coneAngle),
    Editable,
    0.0f, 180.0f,
    "Angle in degrees the axis can lean away from where it started",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::ConeConstraint::descriptor = {
    "ConeConstraint",
    {
        &ConeConstraint_target_info,
        &ConeConstraint_anchor_info,
        &ConeConstraint_collideConnected_info,
        &ConeConstraint_breakForce_info,
        &ConeConstraint_breakTorque_info,
        &ConeConstraint_axis_info,
        &ConeConstraint_coneAngle_info,
    }
};

// Reflection for class SwingTwistConstraint

inline FieldInfo SwingTwistConstraint_target_info = {
    "target",
    TypeID::TagQuery,
    offsetof(Shard::Engine::Objects::Components::SwingTwistConstraint, target),
    Editable,
    0, 0,
    "Tag query of the actor this one is connected to, for example Door.Frame. The first match with a physics body is used. Empty connects to the world, so the actor hangs in place",
    nullptr,
    nullptr,
    &CopyConstruct<Core::TagQuery>,
    &Assign<Core::TagQuery>,
    &Destroy<Core::TagQuery>,
    &Equals<Core::TagQuery>
};

inline FieldInfo SwingTwistConstraint_anchor_info = {
    "anchor",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::SwingTwistConstraint, anchor),
    Editable,
    0, 0,
    "Where the constraint pivots, in the space of this actor",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo SwingTwistConstraint_collideConnected_info = {
    "collideConnected",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::SwingTwistConstraint, collideConnected),
    Editable,
    0, 0,
    "Off lets the two connected bodies collide with each other",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo SwingTwistConstraint_breakForce_info = {
    "breakForce",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SwingTwistConstraint, breakForce),
    Editable,
    0.0f, 1000000000.0f,
    "Force in newtons above which the constraint breaks. 0 never breaks",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SwingTwistConstraint_breakTorque_info = {
    "breakTorque",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SwingTwistConstraint, breakTorque),
    Editable,
    0.0f, 1000000000.0f,
    "Torque in newton metres above which the constraint breaks. 0 never breaks",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SwingTwistConstraint_twistAxis_info = {
    "twistAxis",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::SwingTwistConstraint, twistAxis),
    Editable,
    0, 0,
    "Axis of the twist, along the bone for a limb, in the space of this actor",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

static EnumDescriptor SwingShape_descriptor = {
    "SwingShape",
    {
        { 0, "Cone" },
        { 1, "Pyramid" },
    },
    sizeof(Physics::SwingShape)
};

inline FieldInfo SwingTwistConstraint_swingShape_info = {
    "swingShape",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::SwingTwistConstraint, swingShape),
    Editable,
    0, 0,
    "Cone is a round range of swing, Pyramid a square one",
    nullptr,
    &SwingShape_descriptor,
    &CopyConstruct<Physics::SwingShape>,
    &Assign<Physics::SwingShape>,
    &Destroy<Physics::SwingShape>,
    &Equals<Physics::SwingShape>
};

inline FieldInfo SwingTwistConstraint_swingAngle_info = {
    "swingAngle",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SwingTwistConstraint, swingAngle),
    Editable,
    0.0f, 180.0f,
    "Swing in degrees away from the twist axis, in one direction",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SwingTwistConstraint_planeAngle_info = {
    "planeAngle",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SwingTwistConstraint, planeAngle),
    Editable,
    0.0f, 180.0f,
    "Swing in degrees away from the twist axis, in the perpendicular direction",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SwingTwistConstraint_twistMin_info = {
    "twistMin",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SwingTwistConstraint, twistMin),
    Editable,
    -180.0f, 180.0f,
    "Lowest twist in degrees",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SwingTwistConstraint_twistMax_info = {
    "twistMax",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SwingTwistConstraint, twistMax),
    Editable,
    -180.0f, 180.0f,
    "Highest twist in degrees",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SwingTwistConstraint_friction_info = {
    "friction",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SwingTwistConstraint, friction),
    Editable,
    0.0f, 100000.0f,
    "Torque in newton metres that resists the movement",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::SwingTwistConstraint::descriptor = {
    "SwingTwistConstraint",
    {
        &SwingTwistConstraint_target_info,
        &SwingTwistConstraint_anchor_info,
        &SwingTwistConstraint_collideConnected_info,
        &SwingTwistConstraint_breakForce_info,
        &SwingTwistConstraint_breakTorque_info,
        &SwingTwistConstraint_twistAxis_info,
        &SwingTwistConstraint_swingShape_info,
        &SwingTwistConstraint_swingAngle_info,
        &SwingTwistConstraint_planeAngle_info,
        &SwingTwistConstraint_twistMin_info,
        &SwingTwistConstraint_twistMax_info,
        &SwingTwistConstraint_friction_info,
    }
};


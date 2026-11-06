#pragma once

#include "gravity_field.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

// Reflection for class UniformGravityField

inline FieldInfo UniformGravityField_strength_info = {
    "strength",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::UniformGravityField, strength),
    Editable,
    -1000.0f, 1000.0f,
    "Acceleration in world units per second squared (9.81 for the Earth, in a project in metres). Negative pushes away",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo UniformGravityField_priority_info = {
    "priority",
    TypeID::Int32,
    offsetof(Shard::Engine::Objects::Components::UniformGravityField, priority),
    Editable,
    -100, 100,
    "The field of the highest priority that covers a body wins over the lower ones and over the project gravity",
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

static EnumDescriptor GravityBlend_descriptor = {
    "GravityBlend",
    {
        { 0, "Override" },
        { 1, "Additive" },
    },
    sizeof(Physics::GravityBlend)
};

inline FieldInfo UniformGravityField_blend_info = {
    "blend",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::UniformGravityField, blend),
    Editable,
    0, 0,
    "Override replaces the gravity below it, Additive is added on top of it",
    nullptr,
    &GravityBlend_descriptor,
    &CopyConstruct<Physics::GravityBlend>,
    &Assign<Physics::GravityBlend>,
    &Destroy<Physics::GravityBlend>,
    &Equals<Physics::GravityBlend>
};

inline FieldInfo UniformGravityField_direction_info = {
    "direction",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::UniformGravityField, direction),
    Editable,
    0, 0,
    "Direction gravity points to, in the space of the actor. Down by default",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline ClassDescriptor Shard::Engine::Objects::Components::UniformGravityField::descriptor = {
    "UniformGravityField",
    {
        &UniformGravityField_strength_info,
        &UniformGravityField_priority_info,
        &UniformGravityField_blend_info,
        &UniformGravityField_direction_info,
    }
};

// Reflection for class PointGravityField

inline FieldInfo PointGravityField_strength_info = {
    "strength",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::PointGravityField, strength),
    Editable,
    -1000.0f, 1000.0f,
    "Acceleration in world units per second squared (9.81 for the Earth, in a project in metres). Negative pushes away",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo PointGravityField_priority_info = {
    "priority",
    TypeID::Int32,
    offsetof(Shard::Engine::Objects::Components::PointGravityField, priority),
    Editable,
    -100, 100,
    "The field of the highest priority that covers a body wins over the lower ones and over the project gravity",
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline FieldInfo PointGravityField_blend_info = {
    "blend",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::PointGravityField, blend),
    Editable,
    0, 0,
    "Override replaces the gravity below it, Additive is added on top of it",
    nullptr,
    &GravityBlend_descriptor,
    &CopyConstruct<Physics::GravityBlend>,
    &Assign<Physics::GravityBlend>,
    &Destroy<Physics::GravityBlend>,
    &Equals<Physics::GravityBlend>
};

inline FieldInfo PointGravityField_radius_info = {
    "radius",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::PointGravityField, radius),
    Editable,
    0.0f, 1000000.0f,
    "Distance from the actor beyond which the field does nothing. 0 reaches everywhere",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

static EnumDescriptor GravityFalloff_descriptor = {
    "GravityFalloff",
    {
        { 0, "Constant" },
        { 1, "Linear" },
        { 2, "InverseSquare" },
    },
    sizeof(Physics::GravityFalloff)
};

inline FieldInfo PointGravityField_falloff_info = {
    "falloff",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::PointGravityField, falloff),
    Editable,
    0, 0,
    "How the strength drops with the distance : Constant, Linear (to nothing at the radius) or InverseSquare",
    nullptr,
    &GravityFalloff_descriptor,
    &CopyConstruct<Physics::GravityFalloff>,
    &Assign<Physics::GravityFalloff>,
    &Destroy<Physics::GravityFalloff>,
    &Equals<Physics::GravityFalloff>
};

inline FieldInfo PointGravityField_referenceDistance_info = {
    "referenceDistance",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::PointGravityField, referenceDistance),
    Editable,
    0.001f, 10000.0f,
    "InverseSquare only : the distance at which the strength is reached",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo PointGravityField_minDistance_info = {
    "minDistance",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::PointGravityField, minDistance),
    Editable,
    0.0f, 1000.0f,
    "InverseSquare only : closer than this the strength stops growing, so the pull does not become infinite at the center",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo PointGravityField_fadeDistance_info = {
    "fadeDistance",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::PointGravityField, fadeDistance),
    Editable,
    0.0f, 10000.0f,
    "The gravity fades to nothing over this distance inside the radius, so it does not stop sharply",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::PointGravityField::descriptor = {
    "PointGravityField",
    {
        &PointGravityField_strength_info,
        &PointGravityField_priority_info,
        &PointGravityField_blend_info,
        &PointGravityField_radius_info,
        &PointGravityField_falloff_info,
        &PointGravityField_referenceDistance_info,
        &PointGravityField_minDistance_info,
        &PointGravityField_fadeDistance_info,
    }
};

// Reflection for class RadialGravityField

inline FieldInfo RadialGravityField_strength_info = {
    "strength",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::RadialGravityField, strength),
    Editable,
    -1000.0f, 1000.0f,
    "Acceleration in world units per second squared (9.81 for the Earth, in a project in metres). Negative pushes away",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo RadialGravityField_priority_info = {
    "priority",
    TypeID::Int32,
    offsetof(Shard::Engine::Objects::Components::RadialGravityField, priority),
    Editable,
    -100, 100,
    "The field of the highest priority that covers a body wins over the lower ones and over the project gravity",
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline FieldInfo RadialGravityField_blend_info = {
    "blend",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::RadialGravityField, blend),
    Editable,
    0, 0,
    "Override replaces the gravity below it, Additive is added on top of it",
    nullptr,
    &GravityBlend_descriptor,
    &CopyConstruct<Physics::GravityBlend>,
    &Assign<Physics::GravityBlend>,
    &Destroy<Physics::GravityBlend>,
    &Equals<Physics::GravityBlend>
};

inline FieldInfo RadialGravityField_radius_info = {
    "radius",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::RadialGravityField, radius),
    Editable,
    0.01f, 10000000.0f,
    "Radius of the planet. The strength is the one of the surface",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo RadialGravityField_reach_info = {
    "reach",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::RadialGravityField, reach),
    Editable,
    0.0f, 10000000.0f,
    "Height above the surface to which the gravity reaches. 0 reaches everywhere",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo RadialGravityField_falloff_info = {
    "falloff",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::RadialGravityField, falloff),
    Editable,
    0, 0,
    "How the strength drops with the height above the surface : Constant, Linear (to nothing at the reach) or InverseSquare",
    nullptr,
    &GravityFalloff_descriptor,
    &CopyConstruct<Physics::GravityFalloff>,
    &Assign<Physics::GravityFalloff>,
    &Destroy<Physics::GravityFalloff>,
    &Equals<Physics::GravityFalloff>
};

inline FieldInfo RadialGravityField_solidInterior_info = {
    "solidInterior",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::RadialGravityField, solidInterior),
    Editable,
    0, 0,
    "Inside the planet the gravity decreases to nothing at the center, as in a ball of uniform density. Off keeps the surface strength",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo RadialGravityField_fadeDistance_info = {
    "fadeDistance",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::RadialGravityField, fadeDistance),
    Editable,
    0.0f, 1000000.0f,
    "The gravity fades to nothing over this height at the end of the reach",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::RadialGravityField::descriptor = {
    "RadialGravityField",
    {
        &RadialGravityField_strength_info,
        &RadialGravityField_priority_info,
        &RadialGravityField_blend_info,
        &RadialGravityField_radius_info,
        &RadialGravityField_reach_info,
        &RadialGravityField_falloff_info,
        &RadialGravityField_solidInterior_info,
        &RadialGravityField_fadeDistance_info,
    }
};

// Reflection for class GravityVolume

inline FieldInfo GravityVolume_strength_info = {
    "strength",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::GravityVolume, strength),
    Editable,
    -1000.0f, 1000.0f,
    "Acceleration in world units per second squared (9.81 for the Earth, in a project in metres). Negative pushes away",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo GravityVolume_priority_info = {
    "priority",
    TypeID::Int32,
    offsetof(Shard::Engine::Objects::Components::GravityVolume, priority),
    Editable,
    -100, 100,
    "The field of the highest priority that covers a body wins over the lower ones and over the project gravity",
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline FieldInfo GravityVolume_blend_info = {
    "blend",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::GravityVolume, blend),
    Editable,
    0, 0,
    "Override replaces the gravity below it, Additive is added on top of it",
    nullptr,
    &GravityBlend_descriptor,
    &CopyConstruct<Physics::GravityBlend>,
    &Assign<Physics::GravityBlend>,
    &Destroy<Physics::GravityBlend>,
    &Equals<Physics::GravityBlend>
};

inline FieldInfo GravityVolume_halfExtent_info = {
    "halfExtent",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::GravityVolume, halfExtent),
    Editable,
    0, 0,
    "Half of the size of the box of the volume along each axis, before the scale of the actor",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

static EnumDescriptor GravityVolumeShape_descriptor = {
    "GravityVolumeShape",
    {
        { 0, "Box" },
        { 1, "Sphere" },
        { 2, "Cylinder" },
    },
    sizeof(Physics::GravityVolumeShape)
};

inline FieldInfo GravityVolume_shape_info = {
    "shape",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::GravityVolume, shape),
    Editable,
    0, 0,
    "Shape of the region the volume covers, inscribed in its box. Cylinder stands along Y",
    nullptr,
    &GravityVolumeShape_descriptor,
    &CopyConstruct<Physics::GravityVolumeShape>,
    &Assign<Physics::GravityVolumeShape>,
    &Destroy<Physics::GravityVolumeShape>,
    &Equals<Physics::GravityVolumeShape>
};

static EnumDescriptor GravityVolumeDirection_descriptor = {
    "GravityVolumeDirection",
    {
        { 0, "Fixed" },
        { 1, "ToCenter" },
        { 2, "ToAxis" },
    },
    sizeof(Physics::GravityVolumeDirection)
};

inline FieldInfo GravityVolume_directionMode_info = {
    "directionMode",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::GravityVolume, directionMode),
    Editable,
    0, 0,
    "Where gravity points : Fixed (the direction below), ToCenter, or ToAxis (the Y axis of the volume, away from it with a negative strength)",
    nullptr,
    &GravityVolumeDirection_descriptor,
    &CopyConstruct<Physics::GravityVolumeDirection>,
    &Assign<Physics::GravityVolumeDirection>,
    &Destroy<Physics::GravityVolumeDirection>,
    &Equals<Physics::GravityVolumeDirection>
};

inline FieldInfo GravityVolume_direction_info = {
    "direction",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::GravityVolume, direction),
    Editable,
    0, 0,
    "Fixed only : direction gravity points to, in the space of the actor. Down by default",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo GravityVolume_fadeDistance_info = {
    "fadeDistance",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::GravityVolume, fadeDistance),
    Editable,
    0.0f, 1000.0f,
    "The gravity fades in over this distance inside the edge of the volume. 0 changes sharply at the edge",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::GravityVolume::descriptor = {
    "GravityVolume",
    {
        &GravityVolume_strength_info,
        &GravityVolume_priority_info,
        &GravityVolume_blend_info,
        &GravityVolume_halfExtent_info,
        &GravityVolume_shape_info,
        &GravityVolume_directionMode_info,
        &GravityVolume_direction_info,
        &GravityVolume_fadeDistance_info,
    }
};


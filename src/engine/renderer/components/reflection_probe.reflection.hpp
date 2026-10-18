#pragma once

#include "reflection_probe.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

// Reflection for class ReflectionProbe

inline FieldInfo ReflectionProbe_halfExtent_info = {
    "halfExtent",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::ReflectionProbe, halfExtent),
    Editable,
    0, 0,
    "Half of the size of the box along each axis",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

static EnumDescriptor ReflectionProbeShape_descriptor = {
    "ReflectionProbeShape",
    {
        { 0, "Box" },
        { 1, "Sphere" },
        { 2, "Cylinder" },
        { 3, "Cone" },
        { 4, "Pyramid" },
    },
    sizeof(Rendering::ReflectionProbeShape)
};

inline FieldInfo ReflectionProbe_shape_info = {
    "shape",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::ReflectionProbe, shape),
    Editable,
    0, 0,
    "Shape of the region the probe covers, inscribed in its box",
    nullptr,
    &ReflectionProbeShape_descriptor,
    &CopyConstruct<Rendering::ReflectionProbeShape>,
    &Assign<Rendering::ReflectionProbeShape>,
    &Destroy<Rendering::ReflectionProbeShape>,
    &Equals<Rendering::ReflectionProbeShape>
};

inline FieldInfo ReflectionProbe_bakeResolution_info = {
    "bakeResolution",
    TypeID::Int32,
    offsetof(Shard::Engine::Objects::Components::ReflectionProbe, bakeResolution),
    Editable,
    128, 4096,
    "Width in pixels of the baked picture. The height is half of it",
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline FieldInfo ReflectionProbe_bakeSamples_info = {
    "bakeSamples",
    TypeID::Int32,
    offsetof(Shard::Engine::Objects::Components::ReflectionProbe, bakeSamples),
    Editable,
    1, 4096,
    "Samples per pixel of the bake",
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline FieldInfo ReflectionProbe_bakeBounces_info = {
    "bakeBounces",
    TypeID::Int32,
    offsetof(Shard::Engine::Objects::Components::ReflectionProbe, bakeBounces),
    Editable,
    1, 16,
    "Light bounces of the bake",
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline FieldInfo ReflectionProbe_intensity_info = {
    "intensity",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ReflectionProbe, intensity),
    Editable,
    0.0f, 8.0f,
    "Multiplier on the light of the reflection. 1 is as baked",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo ReflectionProbe_blendDistance_info = {
    "blendDistance",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ReflectionProbe, blendDistance),
    Editable,
    0.0f, 20.0f,
    "Distance inside the edge of the box over which the reflection fades into the sky reflection",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo ReflectionProbe_boxProjection_info = {
    "boxProjection",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::ReflectionProbe, boxProjection),
    Editable,
    0, 0,
    "Corrects the reflection for the size of the box, which a room needs. Off for an open area",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline ClassDescriptor Shard::Engine::Objects::Components::ReflectionProbe::descriptor = {
    "ReflectionProbe",
    {
        &ReflectionProbe_halfExtent_info,
        &ReflectionProbe_shape_info,
        &ReflectionProbe_bakeResolution_info,
        &ReflectionProbe_bakeSamples_info,
        &ReflectionProbe_bakeBounces_info,
        &ReflectionProbe_intensity_info,
        &ReflectionProbe_blendDistance_info,
        &ReflectionProbe_boxProjection_info,
    }
};


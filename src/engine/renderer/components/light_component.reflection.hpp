#pragma once

#include "light_component.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

// Reflection for class Light

static EnumDescriptor LightType_descriptor = {
    "LightType",
    {
        { 0, "Directional" },
        { 1, "Point" },
        { 2, "Spot" },
    },
    sizeof(Rendering::LightType)
};

inline FieldInfo Light_lightType_info = {
    "lightType",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::Light, lightType),
    Editable,
    0, 0,
    "Directional, point or spot",
    nullptr,
    &LightType_descriptor,
    &CopyConstruct<Rendering::LightType>,
    &Assign<Rendering::LightType>,
    &Destroy<Rendering::LightType>,
    &Equals<Rendering::LightType>
};

inline FieldInfo Light_intensity_info = {
    "intensity",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::Light, intensity),
    Editable,
    0, 0,
    "Brightness of the light",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo Light_radius_info = {
    "radius",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::Light, radius),
    Editable,
    0, 0,
    "Distance at which the light stops having an effect, point and spot lights",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo Light_color_info = {
    "color",
    TypeID::ColorRGB,
    offsetof(Shard::Engine::Objects::Components::Light, color),
    Editable,
    0, 0,
    "Color of the emitted light",
    nullptr,
    nullptr,
    &CopyConstruct<COL_RGB>,
    &Assign<COL_RGB>,
    &Destroy<COL_RGB>,
    &Equals<COL_RGB>
};

inline FieldInfo Light_outerCutoff_info = {
    "outerCutoff",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::Light, outerCutoff),
    Editable,
    0.0f, 90.0f,
    "Angle in degrees where the cone fades out completely, spot light only",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo Light_innerCutoff_info = {
    "innerCutoff",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::Light, innerCutoff),
    Editable,
    0.0f, 90.0f,
    "Angle in degrees of the full brightness cone, spot light only",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo Light_castShadows_info = {
    "castShadows",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::Light, castShadows),
    Editable,
    0, 0,
    "Objects block this light and cast shadows",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo Light_sourceSize_info = {
    "sourceSize",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::Light, sourceSize),
    Editable,
    0.0f, 50.0f,
    "Size of the emitter for the soft shadows of baking and raytracing. Radius for point and spot lights, angular radius in degrees for a directional one. 0 gives hard shadows",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

static EnumDescriptor LightBakeMode_descriptor = {
    "LightBakeMode",
    {
        { 0, "Dynamic" },
        { 1, "Stationary" },
        { 2, "Static" },
    },
    sizeof(Rendering::LightBakeMode)
};

inline FieldInfo Light_bakeMode_info = {
    "bakeMode",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::Light, bakeMode),
    Editable,
    0, 0,
    "Dynamic is never baked. Stationary has real-time direct light with its bounced light baked. Static is baked entirely into the lightmaps",
    nullptr,
    &LightBakeMode_descriptor,
    &CopyConstruct<Rendering::LightBakeMode>,
    &Assign<Rendering::LightBakeMode>,
    &Destroy<Rendering::LightBakeMode>,
    &Equals<Rendering::LightBakeMode>
};

inline ClassDescriptor Shard::Engine::Objects::Components::Light::descriptor = {
    "Light",
    {
        &Light_lightType_info,
        &Light_intensity_info,
        &Light_radius_info,
        &Light_color_info,
        &Light_outerCutoff_info,
        &Light_innerCutoff_info,
        &Light_castShadows_info,
        &Light_sourceSize_info,
        &Light_bakeMode_info,
    }
};


#pragma once

#include "light_component.hpp"
#include "engine/core/reflection_fields.hpp"

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

inline FieldInfo Light_type_info = {
    "type",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::Light, type),
    Editable,
    0, 0,
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
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline ClassDescriptor Shard::Engine::Objects::Components::Light::descriptor = {
    "Light",
    {
        &Light_type_info,
        &Light_intensity_info,
        &Light_radius_info,
        &Light_color_info,
        &Light_outerCutoff_info,
        &Light_innerCutoff_info,
        &Light_castShadows_info,
    }
};


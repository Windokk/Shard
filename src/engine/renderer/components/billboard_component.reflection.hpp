#pragma once

#include "billboard_component.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

// Reflection for class Billboard

inline FieldInfo Billboard_size_info = {
    "size",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::Billboard, size),
    Editable,
    0.1f, 500.0f,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

static EnumDescriptor BillboardFacingType_descriptor = {
    "BillboardFacingType",
    {
        { 0, "Camera" },
        { 1, "AxisY" },
        { 2, "Fixed" },
    },
    sizeof(Rendering::BillboardFacingType)
};

inline FieldInfo Billboard_facingType_info = {
    "facingType",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::Billboard, facingType),
    Editable,
    0, 0,
    nullptr,
    &BillboardFacingType_descriptor,
    &CopyConstruct<Rendering::BillboardFacingType>,
    &Assign<Rendering::BillboardFacingType>,
    &Destroy<Rendering::BillboardFacingType>,
    &Equals<Rendering::BillboardFacingType>
};

inline FieldInfo Billboard_ScreenSpaceSize_info = {
    "ScreenSpaceSize",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::Billboard, ScreenSpaceSize),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo Billboard_DepthTest_info = {
    "DepthTest",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::Billboard, DepthTest),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo Billboard_EditorOnly_info = {
    "EditorOnly",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::Billboard, EditorOnly),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo Billboard_Pickable_info = {
    "Pickable",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::Billboard, Pickable),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo Billboard_sprite_info = {
    "sprite",
    TypeID::UInt32,
    offsetof(Shard::Engine::Objects::Components::Billboard, sprite),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<uint32_t>,
    &Assign<uint32_t>,
    &Destroy<uint32_t>,
    &Equals<uint32_t>
};

inline FieldInfo Billboard_spritesCount_info = {
    "spritesCount",
    TypeID::Int32,
    offsetof(Shard::Engine::Objects::Components::Billboard, spritesCount),
    Editable,
    1, 64,
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline FieldInfo Billboard_spritePath_info = {
    "spritePath",
    TypeID::String,
    offsetof(Shard::Engine::Objects::Components::Billboard, spritePath),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<std::string>,
    &Assign<std::string>,
    &Destroy<std::string>,
    &Equals<std::string>
};

inline FieldInfo Billboard_tint_info = {
    "tint",
    TypeID::ColorRGBA,
    offsetof(Shard::Engine::Objects::Components::Billboard, tint),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<COL_RGBA>,
    &Assign<COL_RGBA>,
    &Destroy<COL_RGBA>,
    &Equals<COL_RGBA>
};

inline ClassDescriptor Shard::Engine::Objects::Components::Billboard::descriptor = {
    "Billboard",
    {
        &Billboard_size_info,
        &Billboard_facingType_info,
        &Billboard_ScreenSpaceSize_info,
        &Billboard_DepthTest_info,
        &Billboard_EditorOnly_info,
        &Billboard_Pickable_info,
        &Billboard_sprite_info,
        &Billboard_spritesCount_info,
        &Billboard_spritePath_info,
        &Billboard_tint_info,
    }
};


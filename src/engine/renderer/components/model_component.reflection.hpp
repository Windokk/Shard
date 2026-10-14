#pragma once

#include "model_component.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

// Reflection for class Model

inline FieldInfo Model_meshID_info = {
    "meshID",
    TypeID::Asset,
    offsetof(Shard::Engine::Objects::Components::Model, meshID),
    Editable,
    0, 0,
    "The mesh to draw",
    nullptr,
    nullptr,
    &CopyConstruct<Filesystem::AssetID>,
    &Assign<Filesystem::AssetID>,
    &Destroy<Filesystem::AssetID>,
    &Equals<Filesystem::AssetID>
};

static Container Model_materialsID_container = MakeVectorContainer<Shard::Engine::Filesystem::AssetID>();

inline FieldInfo Model_materialsID_info = {
    "materialsID",
    TypeID::Vector,
    offsetof(Shard::Engine::Objects::Components::Model, materialsID),
    Editable,
    0, 0,
    "One material per submesh",
    &Model_materialsID_container,
    nullptr,
    &CopyConstruct<std::vector<Filesystem::AssetID>>,
    &Assign<std::vector<Filesystem::AssetID>>,
    &Destroy<std::vector<Filesystem::AssetID>>,
    &Equals<std::vector<Filesystem::AssetID>>
};

inline FieldInfo Model_staticLighting_info = {
    "staticLighting",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::Model, staticLighting),
    Editable,
    0, 0,
    "Take part in the baked lighting : it blocks and bounces light and gets a lightmap. Turn it off for what moves",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo Model_lightmapResolution_info = {
    "lightmapResolution",
    TypeID::Int32,
    offsetof(Shard::Engine::Objects::Components::Model, lightmapResolution),
    Editable,
    0, 2048,
    "Lightmap size of this model in texels per side. 0 is worked out from the bake texel density",
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline ClassDescriptor Shard::Engine::Objects::Components::Model::descriptor = {
    "Model",
    {
        &Model_meshID_info,
        &Model_materialsID_info,
        &Model_staticLighting_info,
        &Model_lightmapResolution_info,
    }
};


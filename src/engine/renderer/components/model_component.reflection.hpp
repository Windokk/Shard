#pragma once

#include "model_component.hpp"
#include "engine/core/reflection_fields.hpp"

// Reflection for class Model

inline FieldInfo Model_meshID_info = {
    "meshID",
    TypeID::Asset,
    offsetof(Shard::Engine::Objects::Components::Model, meshID),
    Editable,
    0, 0,
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
    &Model_materialsID_container,
    nullptr,
    &CopyConstruct<std::vector<Filesystem::AssetID>>,
    &Assign<std::vector<Filesystem::AssetID>>,
    &Destroy<std::vector<Filesystem::AssetID>>,
    &Equals<std::vector<Filesystem::AssetID>>
};

inline ClassDescriptor Shard::Engine::Objects::Components::Model::descriptor = {
    "Model",
    {
        &Model_meshID_info,
        &Model_materialsID_info,
    }
};


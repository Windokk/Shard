#pragma once

#include "gun.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

// Reflection for class Gun

inline FieldInfo Gun_gunShotIntensity_info = {
    "gunShotIntensity",
    TypeID::Float,
    offsetof(Gun, gunShotIntensity),
    Editable,
    0.0, 1000.0,
    nullptr,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo Gun_bulletLifetime_info = {
    "bulletLifetime",
    TypeID::Float,
    offsetof(Gun, bulletLifetime),
    Editable,
    0.0, 60.0,
    "Seconds before a bullet is destroyed",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo Gun_mesh_info = {
    "mesh",
    TypeID::Asset,
    offsetof(Gun, mesh),
    Editable,
    0, 0,
    "The mesh to shoot",
    nullptr,
    nullptr,
    &CopyConstruct<Shard::Engine::Filesystem::AssetID>,
    &Assign<Shard::Engine::Filesystem::AssetID>,
    &Destroy<Shard::Engine::Filesystem::AssetID>,
    &Equals<Shard::Engine::Filesystem::AssetID>
};

inline FieldInfo Gun_material_info = {
    "material",
    TypeID::Asset,
    offsetof(Gun, material),
    Editable,
    0, 0,
    "The material of the mesh to shoot",
    nullptr,
    nullptr,
    &CopyConstruct<Shard::Engine::Filesystem::AssetID>,
    &Assign<Shard::Engine::Filesystem::AssetID>,
    &Destroy<Shard::Engine::Filesystem::AssetID>,
    &Equals<Shard::Engine::Filesystem::AssetID>
};

inline FieldInfo Gun_sound_info = {
    "sound",
    TypeID::Asset,
    offsetof(Gun, sound),
    Editable,
    0, 0,
    "The sound to play on shoot",
    nullptr,
    nullptr,
    &CopyConstruct<Shard::Engine::Filesystem::AssetID>,
    &Assign<Shard::Engine::Filesystem::AssetID>,
    &Destroy<Shard::Engine::Filesystem::AssetID>,
    &Equals<Shard::Engine::Filesystem::AssetID>
};

inline ClassDescriptor Gun::descriptor = {
    "Gun",
    {
        &Gun_gunShotIntensity_info,
        &Gun_bulletLifetime_info,
        &Gun_mesh_info,
        &Gun_material_info,
        &Gun_sound_info,
    }
};


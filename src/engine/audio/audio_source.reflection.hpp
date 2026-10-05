#pragma once

#include "audio_source.hpp"
#include "engine/core/reflection_fields.hpp"

// Reflection for class AudioSource

inline FieldInfo AudioSource_assetID_info = {
    "assetID",
    TypeID::Asset,
    offsetof(Shard::Engine::Objects::Components::AudioSource, assetID),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<Filesystem::AssetID>,
    &Assign<Filesystem::AssetID>,
    &Destroy<Filesystem::AssetID>,
    &Equals<Filesystem::AssetID>
};

inline FieldInfo AudioSource_volume_info = {
    "volume",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::AudioSource, volume),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo AudioSource_playOnStart_info = {
    "playOnStart",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::AudioSource, playOnStart),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo AudioSource_spatialize_info = {
    "spatialize",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::AudioSource, spatialize),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline ClassDescriptor Shard::Engine::Objects::Components::AudioSource::descriptor = {
    "AudioSource",
    {
        &AudioSource_assetID_info,
        &AudioSource_volume_info,
        &AudioSource_playOnStart_info,
        &AudioSource_spatialize_info,
    }
};


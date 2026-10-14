#pragma once

#include "audio_source.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

// Reflection for class AudioSource

inline FieldInfo AudioSource_assetID_info = {
    "assetID",
    TypeID::Asset,
    offsetof(Shard::Engine::Objects::Components::AudioSource, assetID),
    Editable,
    0, 0,
    "The audio clip to play",
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
    "Playback volume, 1 is the clip at its original level",
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
    "Start playing as soon as the game starts",
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
    "Play in 3D : the volume and panning follow the distance and direction to the listener",
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


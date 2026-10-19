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

inline FieldInfo AudioSource_dopplerLevel_info = {
    "dopplerLevel",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::AudioSource, dopplerLevel),
    Editable,
    0.0f, 5.0f,
    "Doppler effect : how much the pitch follows the movement of the source and of the listener. 0 is off, 1 is physical, more exaggerates it",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo AudioSource_reverbSend_info = {
    "reverbSend",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::AudioSource, reverbSend),
    Editable,
    0.0f, 1.0f,
    "How much of the sound goes through the reverb of the zone the listener is in. 0 keeps it dry",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::AudioSource::descriptor = {
    "AudioSource",
    {
        &AudioSource_assetID_info,
        &AudioSource_volume_info,
        &AudioSource_playOnStart_info,
        &AudioSource_spatialize_info,
        &AudioSource_dopplerLevel_info,
        &AudioSource_reverbSend_info,
    }
};


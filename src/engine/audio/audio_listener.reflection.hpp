#pragma once

#include "audio_listener.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

// Reflection for class AudioListener

inline FieldInfo AudioListener_dopplerFactor_info = {
    "dopplerFactor",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::AudioListener, dopplerFactor),
    Editable,
    0.0f, 5.0f,
    "Scales the doppler effect of every sound heard from here. 0 turns it off",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo AudioListener_speedOfSound_info = {
    "speedOfSound",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::AudioListener, speedOfSound),
    Editable,
    1.0f, 100000.0f,
    "Speed of sound in world units per second (343 for the air at 20 degrees, in a project in metres). Lower it to hear the doppler effect at slower speeds",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::AudioListener::descriptor = {
    "AudioListener",
    {
        &AudioListener_dopplerFactor_info,
        &AudioListener_speedOfSound_info,
    }
};


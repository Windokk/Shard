#pragma once

#include "random_kicker.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

// Reflection for class RandomKicker

inline FieldInfo RandomKicker_interval_info = {
    "interval",
    TypeID::Int32,
    offsetof(RandomKicker, interval),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline FieldInfo RandomKicker_strength_info = {
    "strength",
    TypeID::Float,
    offsetof(RandomKicker, strength),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo RandomKicker_frames_info = {
    "frames",
    TypeID::Int32,
    offsetof(RandomKicker, frames),
    ReadOnly,
    0, 0,
    nullptr,
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline FieldInfo RandomKicker_kicks_info = {
    "kicks",
    TypeID::Int32,
    offsetof(RandomKicker, kicks),
    ReadOnly,
    0, 0,
    nullptr,
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline ClassDescriptor RandomKicker::descriptor = {
    "RandomKicker",
    {
        &RandomKicker_interval_info,
        &RandomKicker_strength_info,
        &RandomKicker_frames_info,
        &RandomKicker_kicks_info,
    }
};


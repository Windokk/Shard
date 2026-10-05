#pragma once

#include "camera.hpp"
#include "engine/core/reflection_fields.hpp"

// Reflection for class Camera

inline FieldInfo Camera_farPlane_info = {
    "farPlane",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::Camera, farPlane),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo Camera_nearPlane_info = {
    "nearPlane",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::Camera, nearPlane),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo Camera_orthographic_info = {
    "orthographic",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::Camera, orthographic),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo Camera_frustumCulling_info = {
    "frustumCulling",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::Camera, frustumCulling),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo Camera_fov_info = {
    "fov",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::Camera, fov),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo Camera_orthoSize_info = {
    "orthoSize",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::Camera, orthoSize),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::Camera::descriptor = {
    "Camera",
    {
        &Camera_farPlane_info,
        &Camera_nearPlane_info,
        &Camera_orthographic_info,
        &Camera_frustumCulling_info,
        &Camera_fov_info,
        &Camera_orthoSize_info,
    }
};


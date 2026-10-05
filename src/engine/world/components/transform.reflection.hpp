#pragma once

#include "transform.hpp"
#include "engine/core/reflection_fields.hpp"

// Reflection for class Transform

inline FieldInfo Transform_position_info = {
    "position",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::Transform, position),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo Transform_rotation_info = {
    "rotation",
    TypeID::Quat,
    offsetof(Shard::Engine::Objects::Components::Transform, rotation),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<glm::quat>,
    &Assign<glm::quat>,
    &Destroy<glm::quat>,
    &Equals<glm::quat>
};

inline FieldInfo Transform_scale_info = {
    "scale",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::Transform, scale),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline ClassDescriptor Shard::Engine::Objects::Components::Transform::descriptor = {
    "Transform",
    {
        &Transform_position_info,
        &Transform_rotation_info,
        &Transform_scale_info,
    }
};


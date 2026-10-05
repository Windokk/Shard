#pragma once

#include "physics_body.hpp"
#include "engine/core/reflection_fields.hpp"

// Reflection for struct SphereParams

inline FieldInfo SphereParams_radius_info = {
    "radius",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SphereParams, radius),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline StructDescriptor SphereParams_descriptor = {
    "SphereParams",
    {
        &SphereParams_radius_info,
    },
    sizeof(Shard::Engine::Objects::Components::SphereParams),
    [](void* p) { new (p) Shard::Engine::Objects::Components::SphereParams(); },
    [](void* p) { static_cast<Shard::Engine::Objects::Components::SphereParams*>(p)->~SphereParams(); },
    [](void* d, const void* s) {
        *static_cast<Shard::Engine::Objects::Components::SphereParams*>(d) =
        *static_cast<const Shard::Engine::Objects::Components::SphereParams*>(s);
    },
    [](const void* a, const void* b) {
        return *static_cast<const Shard::Engine::Objects::Components::SphereParams*>(a)
            == *static_cast<const Shard::Engine::Objects::Components::SphereParams*>(b);
    }
};

// Reflection for struct CapsuleParams

inline FieldInfo CapsuleParams_radius_info = {
    "radius",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::CapsuleParams, radius),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo CapsuleParams_halfHeight_info = {
    "halfHeight",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::CapsuleParams, halfHeight),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline StructDescriptor CapsuleParams_descriptor = {
    "CapsuleParams",
    {
        &CapsuleParams_radius_info,
        &CapsuleParams_halfHeight_info,
    },
    sizeof(Shard::Engine::Objects::Components::CapsuleParams),
    [](void* p) { new (p) Shard::Engine::Objects::Components::CapsuleParams(); },
    [](void* p) { static_cast<Shard::Engine::Objects::Components::CapsuleParams*>(p)->~CapsuleParams(); },
    [](void* d, const void* s) {
        *static_cast<Shard::Engine::Objects::Components::CapsuleParams*>(d) =
        *static_cast<const Shard::Engine::Objects::Components::CapsuleParams*>(s);
    },
    [](const void* a, const void* b) {
        return *static_cast<const Shard::Engine::Objects::Components::CapsuleParams*>(a)
            == *static_cast<const Shard::Engine::Objects::Components::CapsuleParams*>(b);
    }
};

// Reflection for struct BoxParams

inline FieldInfo BoxParams_halfExtent_info = {
    "halfExtent",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::BoxParams, halfExtent),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline StructDescriptor BoxParams_descriptor = {
    "BoxParams",
    {
        &BoxParams_halfExtent_info,
    },
    sizeof(Shard::Engine::Objects::Components::BoxParams),
    [](void* p) { new (p) Shard::Engine::Objects::Components::BoxParams(); },
    [](void* p) { static_cast<Shard::Engine::Objects::Components::BoxParams*>(p)->~BoxParams(); },
    [](void* d, const void* s) {
        *static_cast<Shard::Engine::Objects::Components::BoxParams*>(d) =
        *static_cast<const Shard::Engine::Objects::Components::BoxParams*>(s);
    },
    [](const void* a, const void* b) {
        return *static_cast<const Shard::Engine::Objects::Components::BoxParams*>(a)
            == *static_cast<const Shard::Engine::Objects::Components::BoxParams*>(b);
    }
};

// Reflection for struct CylinderParams

inline FieldInfo CylinderParams_radius_info = {
    "radius",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::CylinderParams, radius),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo CylinderParams_halfHeight_info = {
    "halfHeight",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::CylinderParams, halfHeight),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline StructDescriptor CylinderParams_descriptor = {
    "CylinderParams",
    {
        &CylinderParams_radius_info,
        &CylinderParams_halfHeight_info,
    },
    sizeof(Shard::Engine::Objects::Components::CylinderParams),
    [](void* p) { new (p) Shard::Engine::Objects::Components::CylinderParams(); },
    [](void* p) { static_cast<Shard::Engine::Objects::Components::CylinderParams*>(p)->~CylinderParams(); },
    [](void* d, const void* s) {
        *static_cast<Shard::Engine::Objects::Components::CylinderParams*>(d) =
        *static_cast<const Shard::Engine::Objects::Components::CylinderParams*>(s);
    },
    [](const void* a, const void* b) {
        return *static_cast<const Shard::Engine::Objects::Components::CylinderParams*>(a)
            == *static_cast<const Shard::Engine::Objects::Components::CylinderParams*>(b);
    }
};

// Reflection for class PhysicsBody
//
// Note: the shape list (PhysicsBody::shapes) is intentionally not reflected here - it's a
// vector of structs (each with its own nested InstancedStruct params), which the generic
// reflected-field editor UI (PropertiesPanel::DrawField) has no support for drawing. The editor
// instead draws it with a dedicated custom UI block; see properties_panel.cpp.

static EnumDescriptor EMotionType_descriptor = {
    "EMotionType",
    {
        { 0, "Static" },
        { 1, "Kinematic" },
        { 2, "Dynamic" },
    },
    sizeof(EMotionType)
};

inline FieldInfo PhysicsBody_motionType_info = {
    "motionType",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, motionType),
    Editable,
    0, 0,
    nullptr,
    &EMotionType_descriptor,
    &CopyConstruct<EMotionType>,
    &Assign<EMotionType>,
    &Destroy<EMotionType>,
    &Equals<EMotionType>
};

inline FieldInfo PhysicsBody_overrideMass_info = {
    "overrideMass",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, overrideMass),
    Editable,
    0, 0,
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline FieldInfo PhysicsBody_mass_info = {
    "mass",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, mass),
    Editable,
    0.001f, 100000.0f,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo PhysicsBody_linearDamping_info = {
    "linearDamping",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, linearDamping),
    Editable,
    0.0f, 1.0f,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo PhysicsBody_angularDamping_info = {
    "angularDamping",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, angularDamping),
    Editable,
    0.0f, 1.0f,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo PhysicsBody_gravityFactor_info = {
    "gravityFactor",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, gravityFactor),
    Editable,
    -10.0f, 10.0f,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo PhysicsBody_friction_info = {
    "friction",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, friction),
    Editable,
    0.0f, 1.0f,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo PhysicsBody_restitution_info = {
    "restitution",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, restitution),
    Editable,
    0.0f, 1.0f,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::PhysicsBody::descriptor = {
    "PhysicsBody",
    {
        &PhysicsBody_motionType_info,
        &PhysicsBody_overrideMass_info,
        &PhysicsBody_mass_info,
        &PhysicsBody_linearDamping_info,
        &PhysicsBody_angularDamping_info,
        &PhysicsBody_gravityFactor_info,
        &PhysicsBody_friction_info,
        &PhysicsBody_restitution_info,
    }
};


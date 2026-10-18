#pragma once

#include "physics_body.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

// Reflection for struct SphereParams

inline FieldInfo SphereParams_radius_info = {
    "radius",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::SphereParams, radius),
    Editable,
    0, 0,
    "Radius of the shape",
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
    "Radius of the shape",
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
    "Half of the height of the shape, without the caps for a capsule",
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
    "Half of the size of the box along each axis",
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
    "Radius of the shape",
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
    "Half of the height of the shape, without the caps for a capsule",
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
    "Static never moves, Kinematic is moved by code only, Dynamic is driven by the physics simulation",
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
    "Use the mass below instead of the one computed from the shape",
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
    "Mass in kg, only used when Override Mass is on",
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
    "How fast the body loses its linear velocity, 0 keeps it forever",
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
    "How fast the body loses its spin, 0 keeps it forever",
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
    "Multiplier on the gravity the body feels, 0 floats and a negative value falls upwards",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

static EnumDescriptor GravityMode_descriptor = {
    "GravityMode",
    {
        { 0, "Fields" },
        { 1, "World" },
        { 2, "Custom" },
    },
    sizeof(Physics::GravityMode)
};

inline FieldInfo PhysicsBody_gravityMode_info = {
    "gravityMode",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, gravityMode),
    Editable,
    0, 0,
    "Where the gravity comes from : Fields is the world gravity changed by the gravity fields around, World ignores the fields, Custom is the gravity below",
    nullptr,
    &GravityMode_descriptor,
    &CopyConstruct<Physics::GravityMode>,
    &Assign<Physics::GravityMode>,
    &Destroy<Physics::GravityMode>,
    &Equals<Physics::GravityMode>
};

inline FieldInfo PhysicsBody_customGravity_info = {
    "customGravity",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, customGravity),
    Editable,
    0, 0,
    "Custom mode only : the acceleration of the body in world units per second squared",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo PhysicsBody_friction_info = {
    "friction",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, friction),
    Editable,
    0.0f, 1.0f,
    "Resistance when sliding on another body, 0 is ice and 1 is very grippy",
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
    "Bounciness, 0 does not bounce and 1 bounces back with all its energy",
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
        &PhysicsBody_gravityMode_info,
        &PhysicsBody_customGravity_info,
        &PhysicsBody_friction_info,
        &PhysicsBody_restitution_info,
    }
};


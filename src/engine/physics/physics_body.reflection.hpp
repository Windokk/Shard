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

// Reflection for struct MeshParams

inline FieldInfo MeshParams_mesh_info = {
    "mesh",
    TypeID::Asset,
    offsetof(Shard::Engine::Objects::Components::MeshParams, mesh),
    Editable,
    0, 0,
    "The mesh to collide with. Empty uses the model of this actor",
    nullptr,
    nullptr,
    &CopyConstruct<Filesystem::AssetID>,
    &Assign<Filesystem::AssetID>,
    &Destroy<Filesystem::AssetID>,
    &Equals<Filesystem::AssetID>
};

static EnumDescriptor MeshApproximation_descriptor = {
    "MeshApproximation",
    {
        { 0, "Auto" },
        { 1, "Exact" },
        { 2, "Hull" },
        { 3, "Hulls" },
        { 4, "Bounds" },
    },
    sizeof(Physics::MeshApproximation)
};

inline FieldInfo MeshParams_approximation_info = {
    "approximation",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::MeshParams, approximation),
    Editable,
    0, 0,
    "Auto : exact triangles for a static or kinematic body and convex pieces for a dynamic one. Exact is only for bodies that do not move by physics",
    nullptr,
    &MeshApproximation_descriptor,
    &CopyConstruct<Physics::MeshApproximation>,
    &Assign<Physics::MeshApproximation>,
    &Destroy<Physics::MeshApproximation>,
    &Equals<Physics::MeshApproximation>
};

inline FieldInfo MeshParams_submeshes_info = {
    "submeshes",
    TypeID::String,
    offsetof(Shard::Engine::Objects::Components::MeshParams, submeshes),
    Editable,
    0, 0,
    "Submeshes to use, for example 0 or 1-3 or 0,2. Empty uses all of them",
    nullptr,
    nullptr,
    &CopyConstruct<std::string>,
    &Assign<std::string>,
    &Destroy<std::string>,
    &Equals<std::string>
};

inline FieldInfo MeshParams_convexRadius_info = {
    "convexRadius",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::MeshParams, convexRadius),
    Editable,
    0.0f, 0.5f,
    "Rounding of the convex pieces, in metres. A little helps stability",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo MeshParams_concavity_info = {
    "concavity",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::MeshParams, concavity),
    Editable,
    0.01f, 1.0f,
    "Hulls only : how far a piece may stray from the mesh. Lower is tighter and slower to build",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo MeshParams_maxHulls_info = {
    "maxHulls",
    TypeID::Int32,
    offsetof(Shard::Engine::Objects::Components::MeshParams, maxHulls),
    Editable,
    1, 64,
    "Hulls only : most convex pieces",
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline FieldInfo MeshParams_maxHullVertices_info = {
    "maxHullVertices",
    TypeID::Int32,
    offsetof(Shard::Engine::Objects::Components::MeshParams, maxHullVertices),
    Editable,
    8, 256,
    "Most vertices of a convex piece",
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline FieldInfo MeshParams_edgeAngle_info = {
    "edgeAngle",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::MeshParams, edgeAngle),
    Editable,
    0.0f, 90.0f,
    "Exact only : edges flatter than this angle are smooth, so a body does not catch on the seams of a floor",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo MeshParams_showInViewport_info = {
    "showInViewport",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::MeshParams, showInViewport),
    Editable,
    0, 0,
    "Draws this collider in the viewport when Show Physics Shapes is on. Turn it off for a heavy mesh",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline StructDescriptor MeshParams_descriptor = {
    "MeshParams",
    {
        &MeshParams_mesh_info,
        &MeshParams_approximation_info,
        &MeshParams_submeshes_info,
        &MeshParams_convexRadius_info,
        &MeshParams_concavity_info,
        &MeshParams_maxHulls_info,
        &MeshParams_maxHullVertices_info,
        &MeshParams_edgeAngle_info,
        &MeshParams_showInViewport_info,
    },
    sizeof(Shard::Engine::Objects::Components::MeshParams),
    [](void* p) { new (p) Shard::Engine::Objects::Components::MeshParams(); },
    [](void* p) { static_cast<Shard::Engine::Objects::Components::MeshParams*>(p)->~MeshParams(); },
    [](void* d, const void* s) {
        *static_cast<Shard::Engine::Objects::Components::MeshParams*>(d) =
        *static_cast<const Shard::Engine::Objects::Components::MeshParams*>(s);
    },
    [](const void* a, const void* b) {
        return *static_cast<const Shard::Engine::Objects::Components::MeshParams*>(a)
            == *static_cast<const Shard::Engine::Objects::Components::MeshParams*>(b);
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

inline FieldInfo PhysicsBody_collisionProfile_info = {
    "collisionProfile",
    TypeID::String,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, collisionProfile),
    Editable,
    0, 0,
    "Collision profile of this collider. Empty uses Default.Static or Default.Moving from the motion type",
    nullptr,
    nullptr,
    &CopyConstruct<std::string>,
    &Assign<std::string>,
    &Destroy<std::string>,
    &Equals<std::string>
};

static EnumDescriptor CollisionModeChoice_descriptor = {
    "CollisionModeChoice",
    {
        { 0, "Inherit" },
        { 1, "Solid" },
        { 2, "Sensor" },
        { 3, "QueryOnly" },
        { 4, "Disabled" },
    },
    sizeof(Physics::CollisionModeChoice)
};

inline FieldInfo PhysicsBody_collisionMode_info = {
    "collisionMode",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, collisionMode),
    Editable,
    0, 0,
    "Replaces the mode of the profile, Inherit keeps it",
    nullptr,
    &CollisionModeChoice_descriptor,
    &CopyConstruct<Physics::CollisionModeChoice>,
    &Assign<Physics::CollisionModeChoice>,
    &Destroy<Physics::CollisionModeChoice>,
    &Equals<Physics::CollisionModeChoice>
};

static EnumDescriptor CollisionResponseChoice_descriptor = {
    "CollisionResponseChoice",
    {
        { 0, "Inherit" },
        { 1, "Ignore" },
        { 2, "Overlap" },
        { 3, "Block" },
    },
    sizeof(Physics::CollisionResponseChoice)
};

inline FieldInfo PhysicsBody_collisionDefault_info = {
    "collisionDefault",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, collisionDefault),
    Editable,
    0, 0,
    "Replaces the default response of the profile, Inherit keeps it",
    nullptr,
    &CollisionResponseChoice_descriptor,
    &CopyConstruct<Physics::CollisionResponseChoice>,
    &Assign<Physics::CollisionResponseChoice>,
    &Destroy<Physics::CollisionResponseChoice>,
    &Equals<Physics::CollisionResponseChoice>
};

inline FieldInfo PhysicsBody_collisionIgnore_info = {
    "collisionIgnore",
    TypeID::String,
    offsetof(Shard::Engine::Objects::Components::PhysicsBody, collisionIgnore),
    Editable,
    0, 0,
    "Tag query for the colliders this one ignores, for example Enemy or Prop | Pickup",
    nullptr,
    nullptr,
    &CopyConstruct<std::string>,
    &Assign<std::string>,
    &Destroy<std::string>,
    &Equals<std::string>
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
        &PhysicsBody_collisionProfile_info,
        &PhysicsBody_collisionMode_info,
        &PhysicsBody_collisionDefault_info,
        &PhysicsBody_collisionIgnore_info,
        &PhysicsBody_restitution_info,
    }
};


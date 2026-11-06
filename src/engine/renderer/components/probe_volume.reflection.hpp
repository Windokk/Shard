#pragma once

#include "probe_volume.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

// Reflection for class ProbeVolume

inline FieldInfo ProbeVolume_halfExtent_info = {
    "halfExtent",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::ProbeVolume, halfExtent),
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

inline FieldInfo ProbeVolume_probeCounts_info = {
    "probeCounts",
    TypeID::IVec3,
    offsetof(Shard::Engine::Objects::Components::ProbeVolume, probeCounts),
    Editable,
    1, 0,
    "Number of probes along each axis of the grid",
    nullptr,
    nullptr,
    &CopyConstruct<glm::ivec3>,
    &Assign<glm::ivec3>,
    &Destroy<glm::ivec3>,
    &Equals<glm::ivec3>
};

inline FieldInfo ProbeVolume_raysPerProbe_info = {
    "raysPerProbe",
    TypeID::Int32,
    offsetof(Shard::Engine::Objects::Components::ProbeVolume, raysPerProbe),
    Editable,
    1, 256,
    "Rays traced per probe and update, a perfect square such as 64 or 256. More is less noisy but costly",
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline FieldInfo ProbeVolume_probeUpdateStride_info = {
    "probeUpdateStride",
    TypeID::Int32,
    offsetof(Shard::Engine::Objects::Components::ProbeVolume, probeUpdateStride),
    Editable,
    1, 8,
    "Update only 1/N of the probes each frame. The main performance knob, large coarse volumes can use 2 to 4",
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline FieldInfo ProbeVolume_indirectIntensity_info = {
    "indirectIntensity",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ProbeVolume, indirectIntensity),
    Editable,
    0.0f, 4.0f,
    "Multiplier on the bounced light fed back into the probes. 1 is physically correct",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo ProbeVolume_enableRelocation_info = {
    "enableRelocation",
    TypeID::Bool,
    offsetof(Shard::Engine::Objects::Components::ProbeVolume, enableRelocation),
    Editable,
    0, 0,
    "Nudge probes out of the geometry they sit inside, so they still give useful data",
    nullptr,
    nullptr,
    &CopyConstruct<bool>,
    &Assign<bool>,
    &Destroy<bool>,
    &Equals<bool>
};

inline ClassDescriptor Shard::Engine::Objects::Components::ProbeVolume::descriptor = {
    "ProbeVolume",
    {
        &ProbeVolume_halfExtent_info,
        &ProbeVolume_probeCounts_info,
        &ProbeVolume_raysPerProbe_info,
        &ProbeVolume_probeUpdateStride_info,
        &ProbeVolume_indirectIntensity_info,
        &ProbeVolume_enableRelocation_info,
    }
};


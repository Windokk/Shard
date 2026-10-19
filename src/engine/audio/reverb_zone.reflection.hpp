#pragma once

#include "reverb_zone.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

// Reflection for class ReverbZone

static EnumDescriptor ReverbShape_descriptor = {
    "ReverbShape",
    {
        { 0, "Box" },
        { 1, "Sphere" },
    },
    sizeof(Audio::ReverbShape)
};

inline FieldInfo ReverbZone_shape_info = {
    "shape",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::ReverbZone, shape),
    Editable,
    0, 0,
    "Shape of the region the zone covers. Box follows the rotation of the actor",
    nullptr,
    &ReverbShape_descriptor,
    &CopyConstruct<Audio::ReverbShape>,
    &Assign<Audio::ReverbShape>,
    &Destroy<Audio::ReverbShape>,
    &Equals<Audio::ReverbShape>
};

inline FieldInfo ReverbZone_halfExtent_info = {
    "halfExtent",
    TypeID::Vec3,
    offsetof(Shard::Engine::Objects::Components::ReverbZone, halfExtent),
    Editable,
    0, 0,
    "Box only : half of the size of the zone along each axis, before the scale of the actor",
    nullptr,
    nullptr,
    &CopyConstruct<glm::vec3>,
    &Assign<glm::vec3>,
    &Destroy<glm::vec3>,
    &Equals<glm::vec3>
};

inline FieldInfo ReverbZone_radius_info = {
    "radius",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ReverbZone, radius),
    Editable,
    0.0f, 100000.0f,
    "Sphere only : radius of the zone, before the scale of the actor",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo ReverbZone_blendDistance_info = {
    "blendDistance",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ReverbZone, blendDistance),
    Editable,
    0.0f, 10000.0f,
    "The reverb fades in over this distance outside the edge of the zone, so entering it is not abrupt. 0 is a hard edge",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo ReverbZone_priority_info = {
    "priority",
    TypeID::Int32,
    offsetof(Shard::Engine::Objects::Components::ReverbZone, priority),
    Editable,
    -100, 100,
    "The zone of the highest priority that covers the listener wins over the lower ones. Zones of the same priority blend",
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

static EnumDescriptor ReverbPreset_descriptor = {
    "ReverbPreset",
    {
        { 0, "Custom" },
        { 1, "Generic" },
        { 2, "Room" },
        { 3, "Bathroom" },
        { 4, "LivingRoom" },
        { 5, "StoneRoom" },
        { 6, "Auditorium" },
        { 7, "ConcertHall" },
        { 8, "Cave" },
        { 9, "Arena" },
        { 10, "Hangar" },
        { 11, "Corridor" },
        { 12, "Sewer" },
        { 13, "Underwater" },
    },
    sizeof(Audio::ReverbPreset)
};

inline FieldInfo ReverbZone_preset_info = {
    "preset",
    TypeID::Enum,
    offsetof(Shard::Engine::Objects::Components::ReverbZone, preset),
    Editable,
    0, 0,
    "A ready-made room. Picking one fills the settings below, changing one of them goes back to Custom",
    nullptr,
    &ReverbPreset_descriptor,
    &CopyConstruct<Audio::ReverbPreset>,
    &Assign<Audio::ReverbPreset>,
    &Destroy<Audio::ReverbPreset>,
    &Equals<Audio::ReverbPreset>
};

inline FieldInfo ReverbZone_roomSize_info = {
    "roomSize",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ReverbZone, roomSize),
    Editable,
    0.0f, 1.0f,
    "Size of the room : the larger, the longer the tail of the reverb",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo ReverbZone_damping_info = {
    "damping",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ReverbZone, damping),
    Editable,
    0.0f, 1.0f,
    "How fast the high frequencies die in the tail. Soft materials (carpet, curtains) damp a lot, stone and tiles do not",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo ReverbZone_width_info = {
    "width",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ReverbZone, width),
    Editable,
    0.0f, 1.0f,
    "Stereo spread of the tail. 0 is mono",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo ReverbZone_wetLevel_info = {
    "wetLevel",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ReverbZone, wetLevel),
    Editable,
    0.0f, 2.0f,
    "Level of the reverberated sound, relative to the sound that goes into it",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo ReverbZone_dryLevel_info = {
    "dryLevel",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ReverbZone, dryLevel),
    Editable,
    0.0f, 1.0f,
    "Level of the direct sound while the listener is in the zone. Lower it to push the sound back into the room",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo ReverbZone_preDelayMs_info = {
    "preDelayMs",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ReverbZone, preDelayMs),
    Editable,
    0.0f, 250.0f,
    "Delay before the reverb starts, in milliseconds. Gives the size of the room to the ear",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo ReverbZone_lowCutHz_info = {
    "lowCutHz",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ReverbZone, lowCutHz),
    Editable,
    10.0f, 2000.0f,
    "The reverberated sound has nothing below this frequency (Hz). Keeps the tail from getting muddy",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo ReverbZone_highCutHz_info = {
    "highCutHz",
    TypeID::Float,
    offsetof(Shard::Engine::Objects::Components::ReverbZone, highCutHz),
    Editable,
    500.0f, 20000.0f,
    "The reverberated sound has nothing above this frequency (Hz). Lower it for a duller room",
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline ClassDescriptor Shard::Engine::Objects::Components::ReverbZone::descriptor = {
    "ReverbZone",
    {
        &ReverbZone_shape_info,
        &ReverbZone_halfExtent_info,
        &ReverbZone_radius_info,
        &ReverbZone_blendDistance_info,
        &ReverbZone_priority_info,
        &ReverbZone_preset_info,
        &ReverbZone_roomSize_info,
        &ReverbZone_damping_info,
        &ReverbZone_width_info,
        &ReverbZone_wetLevel_info,
        &ReverbZone_dryLevel_info,
        &ReverbZone_preDelayMs_info,
        &ReverbZone_lowCutHz_info,
        &ReverbZone_highCutHz_info,
    }
};


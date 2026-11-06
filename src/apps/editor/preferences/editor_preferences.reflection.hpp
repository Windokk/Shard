#pragma once

#include "editor_preferences.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

// Reflection for struct AppearancePreferences

inline FieldInfo AppearancePreferences_uiScale_info = {
    "uiScale",
    TypeID::Float,
    offsetof(Shard::Editor::Core::AppearancePreferences, uiScale),
    Editable,
    0.5f, 3.0f,
    nullptr,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline StructDescriptor AppearancePreferences_descriptor = {
    "AppearancePreferences",
    {
        &AppearancePreferences_uiScale_info,
    },
    sizeof(Shard::Editor::Core::AppearancePreferences),
    [](void* p) { new (p) Shard::Editor::Core::AppearancePreferences(); },
    [](void* p) { static_cast<Shard::Editor::Core::AppearancePreferences*>(p)->~AppearancePreferences(); },
    [](void* d, const void* s) {
        *static_cast<Shard::Editor::Core::AppearancePreferences*>(d) =
        *static_cast<const Shard::Editor::Core::AppearancePreferences*>(s);
    },
    [](const void* a, const void* b) {
        return *static_cast<const Shard::Editor::Core::AppearancePreferences*>(a)
            == *static_cast<const Shard::Editor::Core::AppearancePreferences*>(b);
    }
};

// Reflection for struct CameraPreferences

inline FieldInfo CameraPreferences_speed_info = {
    "speed",
    TypeID::Float,
    offsetof(Shard::Editor::Core::CameraPreferences, speed),
    Editable,
    0.0f, 10000.0f,
    nullptr,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo CameraPreferences_mouseSensitivity_info = {
    "mouseSensitivity",
    TypeID::Float,
    offsetof(Shard::Editor::Core::CameraPreferences, mouseSensitivity),
    Editable,
    0.0f, 10.0f,
    nullptr,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo CameraPreferences_fieldOfView_info = {
    "fieldOfView",
    TypeID::Float,
    offsetof(Shard::Editor::Core::CameraPreferences, fieldOfView),
    Editable,
    10.0f, 170.0f,
    nullptr,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo CameraPreferences_nearPlane_info = {
    "nearPlane",
    TypeID::Float,
    offsetof(Shard::Editor::Core::CameraPreferences, nearPlane),
    Editable,
    0.001f, 100.0f,
    nullptr,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo CameraPreferences_farPlane_info = {
    "farPlane",
    TypeID::Float,
    offsetof(Shard::Editor::Core::CameraPreferences, farPlane),
    Editable,
    1.0f, 1000000.0f,
    nullptr,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo CameraPreferences_focusSpeed_info = {
    "focusSpeed",
    TypeID::Float,
    offsetof(Shard::Editor::Core::CameraPreferences, focusSpeed),
    Editable,
    0.0f, 100.0f,
    nullptr,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline StructDescriptor CameraPreferences_descriptor = {
    "CameraPreferences",
    {
        &CameraPreferences_speed_info,
        &CameraPreferences_mouseSensitivity_info,
        &CameraPreferences_fieldOfView_info,
        &CameraPreferences_nearPlane_info,
        &CameraPreferences_farPlane_info,
        &CameraPreferences_focusSpeed_info,
    },
    sizeof(Shard::Editor::Core::CameraPreferences),
    [](void* p) { new (p) Shard::Editor::Core::CameraPreferences(); },
    [](void* p) { static_cast<Shard::Editor::Core::CameraPreferences*>(p)->~CameraPreferences(); },
    [](void* d, const void* s) {
        *static_cast<Shard::Editor::Core::CameraPreferences*>(d) =
        *static_cast<const Shard::Editor::Core::CameraPreferences*>(s);
    },
    [](const void* a, const void* b) {
        return *static_cast<const Shard::Editor::Core::CameraPreferences*>(a)
            == *static_cast<const Shard::Editor::Core::CameraPreferences*>(b);
    }
};

// Reflection for struct SavingPreferences

inline FieldInfo SavingPreferences_autoSaveIntervalMinutes_info = {
    "autoSaveIntervalMinutes",
    TypeID::Float,
    offsetof(Shard::Editor::Core::SavingPreferences, autoSaveIntervalMinutes),
    Editable,
    0.5f, 240.0f,
    nullptr,
    nullptr,
    nullptr,
    &CopyConstruct<float>,
    &Assign<float>,
    &Destroy<float>,
    &Equals<float>
};

inline FieldInfo SavingPreferences_undoHistorySize_info = {
    "undoHistorySize",
    TypeID::Int32,
    offsetof(Shard::Editor::Core::SavingPreferences, undoHistorySize),
    Editable,
    1, 10000,
    nullptr,
    nullptr,
    nullptr,
    &CopyConstruct<int>,
    &Assign<int>,
    &Destroy<int>,
    &Equals<int>
};

inline StructDescriptor SavingPreferences_descriptor = {
    "SavingPreferences",
    {
        &SavingPreferences_autoSaveIntervalMinutes_info,
        &SavingPreferences_undoHistorySize_info,
    },
    sizeof(Shard::Editor::Core::SavingPreferences),
    [](void* p) { new (p) Shard::Editor::Core::SavingPreferences(); },
    [](void* p) { static_cast<Shard::Editor::Core::SavingPreferences*>(p)->~SavingPreferences(); },
    [](void* d, const void* s) {
        *static_cast<Shard::Editor::Core::SavingPreferences*>(d) =
        *static_cast<const Shard::Editor::Core::SavingPreferences*>(s);
    },
    [](const void* a, const void* b) {
        return *static_cast<const Shard::Editor::Core::SavingPreferences*>(a)
            == *static_cast<const Shard::Editor::Core::SavingPreferences*>(b);
    }
};


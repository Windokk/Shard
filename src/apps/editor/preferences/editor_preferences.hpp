#pragma once

#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/assets/reflection/attributes.hpp"

#include <string>

#define PREFERENCE_RANGE(Struct, field)     ((void)sizeof(Struct::field), ::Shard::Editor::Core::GetPreferenceRange(#Struct, #field))

namespace Shard::Editor::Core {

    struct EditorSettings{

        bool showOutlines = true;
        bool showGizmos = true;
        bool showDDGIGizmos = true;
        bool showPhysicsShapes = true;
        bool showBillboards = true;
        bool showLighting = true;
        bool showShadows = true;

        bool showGrid = true;
        bool showGridX = false;
        bool showGridY = true;
        bool showGridZ = false;
        Engine::Rendering::ViewMode viewMode = Engine::Rendering::ViewMode::Lit;

        bool snapLocation = false;
        bool snapRotation = false;
        bool snapScale = false;
        float locationSnap = 1.0f;
        float rotationSnap = 15.0f;
        float scaleSnap = 0.25f;
    };

    struct PanelVisibility{
        bool viewport = true;
        bool worldTree = true;
        bool properties = true;
        bool assetBrowser = true;
        bool console = true;
        bool profiler = true;
        bool worldSettings = true;
        bool projectSettings = false;
        bool editorPreferences = false;
        bool plugins = false;
    };

    enum class EditorTheme : int { Shard = 0, Dark = 1, Light = 2, Classic = 3 };

    struct STRUCT() AppearancePreferences{
        EditorTheme theme = EditorTheme::Shard;
        FIELD(Editable, range=0.5f|3.0f) float uiScale = 1.0f;

        bool operator==(const AppearancePreferences& other) const {
            return theme == other.theme && uiScale == other.uiScale;
        }
    };

    struct STRUCT() CameraPreferences{
        FIELD(Editable, range=0.0f|10000.0f) float speed = 20.0f;
        FIELD(Editable, range=0.0f|10.0f) float mouseSensitivity = 0.1f;
        bool invertY = false;
        FIELD(Editable, range=10.0f|170.0f) float fieldOfView = 60.0f;
        FIELD(Editable, range=0.001f|100.0f) float nearPlane = 0.1f;
        FIELD(Editable, range=1.0f|1000000.0f) float farPlane = 100.0f;
        FIELD(Editable, range=0.0f|100.0f) float focusSpeed = 15.0f;

        bool operator==(const CameraPreferences& other) const {
            return speed == other.speed && mouseSensitivity == other.mouseSensitivity && invertY == other.invertY
                && fieldOfView == other.fieldOfView && nearPlane == other.nearPlane && farPlane == other.farPlane
                && focusSpeed == other.focusSpeed;
        }
    };

    struct STRUCT() SavingPreferences{
        bool autoSaveWorld = false;
        FIELD(Editable, range=0.5f|240.0f) float autoSaveIntervalMinutes = 5.0f;
        FIELD(Editable, range=1|10000) int undoHistorySize = 256;

        bool operator==(const SavingPreferences& other) const {
            return autoSaveWorld == other.autoSaveWorld && autoSaveIntervalMinutes == other.autoSaveIntervalMinutes
                && undoHistorySize == other.undoHistorySize;
        }
    };

    struct PreferenceRange{
        float min = 0.0f;
        float max = 0.0f;
    };

    PreferenceRange GetPreferenceRange(const char* structName, const char* fieldName);

    struct EditorPreferences{
        AppearancePreferences appearance;
        CameraPreferences camera;
        SavingPreferences saving;
        EditorSettings viewport;
        PanelVisibility panels;
    };

    std::string GetEditorPreferencesPath();

    std::string SerializeEditorPreferences(const EditorPreferences& preferences);

    bool LoadEditorPreferences(const std::string& path, EditorPreferences& preferences, std::string* fileText = nullptr);

    bool SaveEditorPreferences(const std::string& path, const EditorPreferences& preferences);
}

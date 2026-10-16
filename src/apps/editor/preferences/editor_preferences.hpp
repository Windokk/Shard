#pragma once

#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/assets/reflection/attributes.hpp"

#include <string>

/// The range of `Struct::field` (an editor preference) from the reflection : PREFERENCE_RANGE(CameraPreferences, fieldOfView).min
#define PREFERENCE_RANGE(Struct, field)     ((void)sizeof(Struct::field), ::Shard::Editor::Core::GetPreferenceRange(#Struct, #field))

namespace Shard::Editor::Core {

    /// What the viewport shows and how its gizmos snap. Toggled from the viewport's toolbar as well as from the
    /// Editor Preferences, and kept from one session to the next with the rest of the preferences.
    struct EditorSettings{

        // Viewport settings
        bool showOutlines = true;
        bool showGizmos = true;
        bool showDDGIGizmos = true;
        bool showPhysicsShapes = true;
        /// The icons of the lights, cameras and probe volumes (and the billboards flagged editor only)
        bool showBillboards = true;
        bool showLighting = true;
        bool showShadows = true;

        // Infinite grid : one toggle per plane, named after the axis the plane is perpendicular to
        // (Y = the XZ floor). Squares match the location snap increment, or 1m with snapping off.
        bool showGrid = true;
        bool showGridX = false;
        bool showGridY = true;
        bool showGridZ = false;
        Engine::Rendering::ViewMode viewMode = Engine::Rendering::ViewMode::Lit;

        // Gizmo snapping
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
    };

    enum class EditorTheme : int { Shard = 0, Dark = 1, Light = 2, Classic = 3 };

    // The ranges of the values (FIELD range=min|max) are the ones the file is read with, and the ones every widget that edits
    // them keeps to (Editor Preferences, the viewport's Scene Camera window) : see GetPreferenceRange.

    struct STRUCT() AppearancePreferences{
        EditorTheme theme = EditorTheme::Shard;
        /// Size of the whole interface (text, padding, spacing), 1 = 100 %
        FIELD(Editable, range=0.5f|3.0f) float uiScale = 1.0f;

        bool operator==(const AppearancePreferences& other) const {
            return theme == other.theme && uiScale == other.uiScale;
        }
    };

    struct STRUCT() CameraPreferences{
        /// Units per second the fly camera moves at
        FIELD(Editable, range=0.0f|10000.0f) float speed = 20.0f;
        /// Degrees turned per pixel the mouse moves
        FIELD(Editable, range=0.0f|10.0f) float mouseSensitivity = 0.1f;
        bool invertY = false;
        /// Vertical, in degrees
        FIELD(Editable, range=10.0f|170.0f) float fieldOfView = 60.0f;
        FIELD(Editable, range=0.001f|100.0f) float nearPlane = 0.1f;
        /// Kept beyond the near plane as well (nearPlane + 0.01 at least)
        FIELD(Editable, range=1.0f|1000000.0f) float farPlane = 100.0f;
        /// How fast "Focus Selected" glides to the selection (exponential rate, per second) ; 0 jumps straight there
        FIELD(Editable, range=0.0f|100.0f) float focusSpeed = 15.0f;

        bool operator==(const CameraPreferences& other) const {
            return speed == other.speed && mouseSensitivity == other.mouseSensitivity && invertY == other.invertY
                && fieldOfView == other.fieldOfView && nearPlane == other.nearPlane && farPlane == other.farPlane
                && focusSpeed == other.focusSpeed;
        }
    };

    struct STRUCT() SavingPreferences{
        /// Saves the open world on its own when it has unsaved changes (never during play mode)
        bool autoSaveWorld = false;
        FIELD(Editable, range=0.5f|240.0f) float autoSaveIntervalMinutes = 5.0f;
        /// How many actions Undo can go back
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

    /// @brief The range of a preference, as its FIELD(range=) annotation gives it (reflection). Use PREFERENCE_RANGE(Struct, field).
    PreferenceRange GetPreferenceRange(const char* structName, const char* fieldName);

    /// @brief Everything the editor remembers for the user, whatever the project : how it looks, how its camera moves, what
    /// the viewport shows, which panels are open. Saved as `editor_preferences.json` next to the executable (beside the
    /// engine's `user.cfg`), as soon as something changes and when the editor closes.
    struct EditorPreferences{
        AppearancePreferences appearance;
        CameraPreferences camera;
        SavingPreferences saving;
        EditorSettings viewport;
        PanelVisibility panels;
    };

    /// The preferences file of this machine's editor (next to the executable)
    std::string GetEditorPreferencesPath();

    /// The preferences as the text of their file
    std::string SerializeEditorPreferences(const EditorPreferences& preferences);

    /// @brief Reads the file into `preferences`. What the file lacks (or gets wrong) keeps the value it had, so an old file
    /// or a hand edit never loses more than the broken entries. False when there is no readable file.
    /// @param fileText receives the file as it is on disk (empty without one) : what it takes to know whether it needs rewriting
    bool LoadEditorPreferences(const std::string& path, EditorPreferences& preferences, std::string* fileText = nullptr);

    bool SaveEditorPreferences(const std::string& path, const EditorPreferences& preferences);
}

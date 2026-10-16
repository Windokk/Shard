#include "editor_preferences_panel.hpp"

#include "apps/editor/gui/main_window.hpp"
#include "apps/editor/gui/popups.hpp"
#include "apps/editor/gui/IconsLucide.h"
#include "apps/editor/gui/panels/asset_browser/asset_operations.hpp"
#include "apps/editor/preferences/editor_preferences.hpp"

#include "imgui/imgui.h"

#include <algorithm>
#include <filesystem>

namespace Shard::Editor::GUI{

    namespace {
        const Core::EditorPreferences kDefaults{};
    }

    EditorPreferencesPanel::EditorPreferencesPanel()
    {
        m_View.SetCategories({
            { "Appearance", ICON_LC_PALETTE, [this]{ DrawAppearanceCategory(); } },
            { "Viewport", ICON_LC_CAMERA, [this]{ DrawViewportCategory(); } },
            { "Snapping", ICON_LC_MAGNET, [this]{ DrawSnappingCategory(); } },
            { "Saving & Undo", ICON_LC_SAVE, [this]{ DrawSavingCategory(); } },
        });
    }

    void EditorPreferencesPanel::Draw(bool* open)
    {
        ImGui::SetNextWindowSize(ImVec2(780, 480), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Editor Preferences", open))
        {
            ImGui::End();
            return;
        }

        if (!m_Parent)
        {
            ImGui::End();
            return;
        }

        const std::string path = Core::GetEditorPreferencesPath();

        // Top bar : search, then the file and the reset of everything
        const char* revealLabel = ICON_LC_FOLDER_OPEN " Show File";
        const char* resetLabel = ICON_LC_ROTATE_CCW " Reset All";
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttonsWidth = ImGui::CalcTextSize(revealLabel).x + ImGui::CalcTextSize(resetLabel).x
                                 + style.FramePadding.x * 4.0f + style.ItemSpacing.x;
        m_View.DrawSearchBar(buttonsWidth);

        ImGui::SameLine();
        ImGui::BeginDisabled(!std::filesystem::exists(path));
        if (ImGui::Button(revealLabel))
            AssetOperations::RevealInFileExplorer(path, true);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", path.c_str());

        ImGui::SameLine();
        if (ImGui::Button(resetLabel))
        {
            Core::EditorMainWindow* parent = m_Parent;
            Popups::Show(Popups::PopupType::Warning, "Reset Editor Preferences",
                "Put every editor preference back to its default ? Which panels are open is kept.",
                {
                    { "Reset", [parent]{
                        Core::PanelVisibility panels = parent->preferences.panels;
                        parent->preferences = Core::EditorPreferences{};
                        parent->preferences.panels = panels;
                    } },
                    { "Cancel", nullptr }
                });
        }

        ImGui::TextDisabled("Saved automatically for this machine, whatever the project.");

        m_View.DrawBody();

        ImGui::End();
    }

    void EditorPreferencesPanel::DrawAppearanceCategory()
    {
        Core::AppearancePreferences& appearance = m_Parent->preferences.appearance;

        m_View.Section("Interface");

        static const char* const themes[] = { "Shard", "Dark", "Light", "Classic" };
        int theme = static_cast<int>(appearance.theme);
        if (m_View.Combo("Theme", "The colours of the interface. Shard is the editor's own ; the others are Dear ImGui's.",
                         theme, static_cast<int>(kDefaults.appearance.theme), themes, IM_ARRAYSIZE(themes)))
            appearance.theme = static_cast<Core::EditorTheme>(theme);

        // Applied when the slider is let go : rescaling under the mouse would move the slider away from it
        bool reset = false;
        if (m_View.BeginRow("UI Scale", "Size of the whole interface : text, padding and spacing.",
                            appearance.uiScale != kDefaults.appearance.uiScale, &reset))
        {
            if (reset)
            {
                appearance.uiScale = kDefaults.appearance.uiScale;
                m_PendingUIScale = -1.0f;
            }

            const Core::PreferenceRange range = PREFERENCE_RANGE(Core::AppearancePreferences, uiScale);
            float percent = (m_PendingUIScale > 0.0f ? m_PendingUIScale : appearance.uiScale) * 100.0f;
            if (ImGui::SliderFloat("##UIScale", &percent, range.min * 100.0f, range.max * 100.0f, "%.0f %%", ImGuiSliderFlags_AlwaysClamp))
                m_PendingUIScale = percent / 100.0f;
            if (ImGui::IsItemDeactivatedAfterEdit() && m_PendingUIScale > 0.0f)
            {
                appearance.uiScale = m_PendingUIScale;
                m_PendingUIScale = -1.0f;
            }
        }
    }

    void EditorPreferencesPanel::DrawViewportCategory()
    {
        Core::CameraPreferences& camera = m_Parent->preferences.camera;
        Core::EditorSettings& viewport = m_Parent->preferences.viewport;
        const Core::CameraPreferences& cameraDefaults = kDefaults.camera;
        const Core::EditorSettings& viewportDefaults = kDefaults.viewport;

        m_View.Section("Camera");

        // The ranges are the fields' own (FIELD range= in editor_preferences.hpp) : the same as the file and the Scene Camera window
        const Core::PreferenceRange speedRange = PREFERENCE_RANGE(Core::CameraPreferences, speed);
        const Core::PreferenceRange sensitivityRange = PREFERENCE_RANGE(Core::CameraPreferences, mouseSensitivity);
        const Core::PreferenceRange fovRange = PREFERENCE_RANGE(Core::CameraPreferences, fieldOfView);
        const Core::PreferenceRange nearRange = PREFERENCE_RANGE(Core::CameraPreferences, nearPlane);
        const Core::PreferenceRange farRange = PREFERENCE_RANGE(Core::CameraPreferences, farPlane);
        const Core::PreferenceRange focusRange = PREFERENCE_RANGE(Core::CameraPreferences, focusSpeed);

        m_View.DragFloat("Camera Speed", "Units per second the viewport camera flies at (W A S D while the left mouse button is held).",
                         camera.speed, cameraDefaults.speed, 0.1f, speedRange.min, speedRange.max, "%.1f u/s");
        m_View.DragFloat("Mouse Sensitivity", "Degrees the camera turns per pixel the mouse moves.",
                         camera.mouseSensitivity, cameraDefaults.mouseSensitivity, 0.002f, sensitivityRange.min, sensitivityRange.max, "%.3f");
        m_View.Checkbox("Invert Y Axis", "Moving the mouse up looks down.", camera.invertY, cameraDefaults.invertY);
        m_View.SliderFloat("Field of View", "Vertical field of view of the viewport camera, in degrees.",
                           camera.fieldOfView, cameraDefaults.fieldOfView, fovRange.min, fovRange.max, "%.0f deg");
        if (m_View.DragFloat("Near Plane", "Closest distance the viewport camera draws. Too small a value costs depth precision far away.",
                             camera.nearPlane, cameraDefaults.nearPlane, 0.005f, nearRange.min, nearRange.max, "%.3f"))
            camera.farPlane = std::max(camera.farPlane, camera.nearPlane + 0.01f);
        if (m_View.DragFloat("Far Plane", "Farthest distance the viewport camera draws.",
                             camera.farPlane, cameraDefaults.farPlane, 1.0f, farRange.min, farRange.max, "%.0f"))
            camera.farPlane = std::max(camera.farPlane, camera.nearPlane + 0.01f);
        m_View.DragFloat("Focus Speed", "How fast Focus Selected (F) glides to the selection. 0 jumps straight there.",
                         camera.focusSpeed, cameraDefaults.focusSpeed, 0.1f, focusRange.min, focusRange.max, "%.1f");

        m_View.Section("Display");

        m_View.Checkbox("Selection Outline", "Outline the selected actor.", viewport.showOutlines, viewportDefaults.showOutlines);
        m_View.Checkbox("Gizmos", "The transform gizmo of the selection and the volumes of the components.", viewport.showGizmos, viewportDefaults.showGizmos);
        m_View.Checkbox("GI Probe Gizmos", "The probes of the DDGI volumes.", viewport.showDDGIGizmos, viewportDefaults.showDDGIGizmos);
        m_View.Checkbox("Physics Shapes", "The colliders of the physics bodies.", viewport.showPhysicsShapes, viewportDefaults.showPhysicsShapes);
        m_View.Checkbox("Billboards", "The icons of the lights, cameras and probe volumes.", viewport.showBillboards, viewportDefaults.showBillboards);
        m_View.Checkbox("Lighting", "Light the scene. Off shows the base colours only.", viewport.showLighting, viewportDefaults.showLighting);
        m_View.Checkbox("Shadows", "Draw the shadows of the lights.", viewport.showShadows, viewportDefaults.showShadows);

        m_View.Section("Grid");

        m_View.Checkbox("Show Grid", "The infinite grid. Its squares match the location snap increment (1 unit with snapping off).",
                        viewport.showGrid, viewportDefaults.showGrid);
        ImGui::BeginDisabled(!viewport.showGrid);
        m_View.Checkbox("YZ Plane (X)", "The grid plane perpendicular to X.", viewport.showGridX, viewportDefaults.showGridX);
        m_View.Checkbox("XZ Plane (Y, floor)", "The grid plane perpendicular to Y : the floor.", viewport.showGridY, viewportDefaults.showGridY);
        m_View.Checkbox("XY Plane (Z)", "The grid plane perpendicular to Z.", viewport.showGridZ, viewportDefaults.showGridZ);
        ImGui::EndDisabled();
    }

    void EditorPreferencesPanel::DrawSnappingCategory()
    {
        Core::EditorSettings& viewport = m_Parent->preferences.viewport;
        const Core::EditorSettings& defaults = kDefaults.viewport;

        m_View.Section("Location");
        m_View.Checkbox("Snap Location", "Moves with the gizmo go by whole increments.", viewport.snapLocation, defaults.snapLocation);
        m_View.DragFloat("Location Increment", "Distance of one location snap, in units. The grid squares follow it.",
                         viewport.locationSnap, defaults.locationSnap, 0.01f, 0.001f, 10000.0f, "%.3f");

        m_View.Section("Rotation");
        m_View.Checkbox("Snap Rotation", "Rotations with the gizmo go by whole increments.", viewport.snapRotation, defaults.snapRotation);
        m_View.DragFloat("Rotation Increment", "Angle of one rotation snap, in degrees.",
                         viewport.rotationSnap, defaults.rotationSnap, 0.5f, 0.01f, 360.0f, "%.1f deg");

        m_View.Section("Scale");
        m_View.Checkbox("Snap Scale", "Scaling with the gizmo goes by whole increments.", viewport.snapScale, defaults.snapScale);
        m_View.DragFloat("Scale Increment", "Step of one scale snap.",
                         viewport.scaleSnap, defaults.scaleSnap, 0.005f, 0.001f, 100.0f, "%.3f");
    }

    void EditorPreferencesPanel::DrawSavingCategory()
    {
        Core::SavingPreferences& saving = m_Parent->preferences.saving;
        const Core::SavingPreferences& defaults = kDefaults.saving;

        m_View.Section("Auto-save");
        m_View.Checkbox("Auto-save World", "Saves the open world on its own when it has unsaved changes. Never during play mode.",
                        saving.autoSaveWorld, defaults.autoSaveWorld);
        const Core::PreferenceRange intervalRange = PREFERENCE_RANGE(Core::SavingPreferences, autoSaveIntervalMinutes);
        const Core::PreferenceRange historyRange = PREFERENCE_RANGE(Core::SavingPreferences, undoHistorySize);

        ImGui::BeginDisabled(!saving.autoSaveWorld);
        m_View.DragFloat("Auto-save Interval", "Minutes between two auto-saves.",
                         saving.autoSaveIntervalMinutes, defaults.autoSaveIntervalMinutes, 0.1f, intervalRange.min, intervalRange.max, "%.1f min");
        ImGui::EndDisabled();

        m_View.Section("Undo");
        m_View.DragInt("Undo History", "How many actions Undo (Ctrl+W) can go back. Lowering it forgets the oldest ones once the next action is recorded.",
                       saving.undoHistorySize, defaults.undoHistorySize, 1.0f, static_cast<int>(historyRange.min), static_cast<int>(historyRange.max));
    }
}

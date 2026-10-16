#pragma once

#include "engine/platform/windowing/iwindow.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/world/actor.hpp"

#include "engine/renderer/rhi/backends/glad/include/glad/gl.h"
#include "engine/platform/windowing/sdl/sdl_window.hpp"
#include "apps/editor/gui/panels/viewport/viewport_window.hpp"
#include "apps/editor/gui/panels/properties/properties_panel.hpp"
#include "apps/editor/gui/panels/asset_browser/asset_browser.hpp"
#include "apps/editor/gui/panels/material_editor/material_editor_panel.hpp"
#include "apps/editor/gui/panels/asset_editor_registry.hpp"
#include "apps/editor/gui/panels/world_tree/world_tree.hpp"
#include "apps/editor/gui/panels/world_settings/world_settings_panel.hpp"
#include "apps/editor/gui/panels/project_settings/project_settings_panel.hpp"
#include "apps/editor/gui/panels/editor_preferences/editor_preferences_panel.hpp"
#include "apps/editor/gui/panels/console/console.hpp"
#include "apps/editor/gui/panels/profiler/profiler_panel.hpp"
#include "apps/editor/gui/panels/menu_bar/menu_bar.hpp"
#include "apps/editor/debug/editor_debug_draw.hpp"
#include "apps/editor/preferences/editor_preferences.hpp"
#include "apps/editor/commands/object_ref.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_opengl3.h"

#include <cstdint>

namespace Shard::Editor::Core {

    enum EditorViewportBuffer{
        Final,
        Shadows,
        ObjectID,
        MatID
    };

    class EditorMainWindow : public Engine::Core::Platform::SDLWindow {
    public:
        void Init(const std::string& title, const int& width, const int& height, 
                    const bool& fullscreen, const int& vsync, const uint32_t& api) override;

        void SwapBuffers() override;

        void DrawLoadingFrame(float progress) override;

        int GetFramebufferWidth() const override;

        int GetFramebufferHeight() const override;

        void Destroy() const override;

        /// Closes only once the unsaved project settings are saved or dropped (UpdateCloseRequest asks)
        bool ShouldClose() const override;
  
        void RequestExit();

        void ProcessInputs() const override;

        void SetSelectedActor(std::shared_ptr<Engine::Objects::Actor> newPtr);

        void RequestPlay();

        std::shared_ptr<Engine::Objects::Actor> GetSelectedActor(){
            return selectedActor;
        }

        /// Drops the selection if its actor was destroyed (with its parent, by an undo...), or moves it to the
        /// actor that an undo or a redo rebuilt in its place
        void ValidateSelection();

        Engine::Core::Platform::SystemInfos GetSystemInfos() const override;

        
        GUI::ViewportWindow* viewport = nullptr;

        /// What the user set up : read from the preferences file when the interface starts, written back when it changes
        EditorPreferences preferences;
        /// What the viewport shows and its snapping (part of the preferences)
        EditorSettings& settings = preferences.viewport;
        /// Which panels are open (part of the preferences)
        PanelVisibility& panelVisibility = preferences.panels;

        /// @brief Restyles the interface from the appearance preferences (theme, scale). Called when they change.
        void ApplyAppearance();

    protected:
        // ImGui reads the SDL events itself
        void OnNativeEvent(const SDL_Event& event) override;

    private:
        void EnsureImGuiInitialized();
        void DrawLoadingOverlay(float progress);

        void OnWorldLoaded();

        // Polls the ProbeManager's async scene-build progress and mirrors it into a single
        // "Baking GI probes" progress notification (see editor/gui/notifications.hpp).
        void UpdateProbeBuildNotification();
        void UpdateLightmapBuildNotification();

        // Applies what changed in the preferences, writes them out when they differ from the file, runs the world auto-save
        void UpdatePreferences();
        void UpdateAutoSave();

        /// Project Settings has changes that are not saved (the project file or project.cfg) : quitting asks about them
        bool HasUnsavedProjectSettings() const;
        /// The window was asked to close while HasUnsavedProjectSettings : asks to save, drop or stay
        void UpdateCloseRequest();

        bool imguiInitialized = false;
        bool renderPassesInitialized = false;

        // Panels
        GUI::AssetBrowser* assetBrowser = nullptr;
        GUI::MaterialEditorPanel* materialEditorPanel = nullptr;
        GUI::PropertiesPanel* propertiesPanel = nullptr;
        GUI::WorldTree* worldTree = nullptr;
        GUI::WorldSettingsPanel* worldSettingsPanel = nullptr;
        GUI::ProjectSettingsPanel* projectSettingsPanel = nullptr;
        GUI::EditorPreferencesPanel* editorPreferencesPanel = nullptr;
        GUI::Console* console = nullptr;
        GUI::ProfilerPanel* profilerPanel = nullptr;
        GUI::MenuBar* menuBar = nullptr;
        
        // What the editor draws over the scene for the components (colliders, volumes, probes)
        Debug::EditorDebugDraw debugDraw;

        // User data
        std::shared_ptr<Engine::Objects::Actor> selectedActor = nullptr;
        // Follows the selected actor through the undo history, which destroys and rebuilds actors
        std::shared_ptr<Editor::Commands::ObjectRef> selectedRef = nullptr;

        // Preferences : the text last written to the file, what the interface was last styled with, the auto-save clock
        std::string savedPreferencesText;
        double nextPreferencesCheck = 0.0;
        AppearancePreferences appliedAppearance;
        double lastAutoSaveTime = 0.0;

        // The user answered the quit prompt with Save or Discard : nothing holds the close back anymore
        bool closeConfirmed = false;

        // The JFA (init/step) and outline composite passes each only ever hold a single, permanent
        // fullscreen-triangle draw command (see the render pass setup in SwapBuffers) - a world reload
        // (Renderer::ClearPassesContent(), see EngineInstance::Run()'s m_ReloadCurrentWorld handling)
        // wipes every pass's draw list, and since that setup only ever runs once, those commands would
        // otherwise never come back. Re-submitted every frame in SwapBuffers alongside the selected
        // actor's mask commands so the outline survives any such reload.
        std::vector<std::pair<std::string, std::shared_ptr<Engine::Rendering::Material>>> outlinePipelineFullscreenCommands;

        // Same story for the three grid planes (X/Y/Z), except these need the camera matrices bound.
        std::vector<std::pair<std::string, std::shared_ptr<Engine::Rendering::Material>>> gridCommands;
    };
}
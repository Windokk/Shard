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
#include "apps/editor/commands/actor_commands.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_opengl3.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

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

        /// Selects this actor alone (null : nothing selected)
        void SetSelectedActor(std::shared_ptr<Engine::Objects::Actor> newPtr);

        void RequestPlay();

        /// The primary actor of the selection : the last one selected. It is the one the properties panel shows and the one
        /// the gizmo sits on.
        std::shared_ptr<Engine::Objects::Actor> GetSelectedActor() const {
            return selectedActors.empty() ? nullptr : selectedActors.back();
        }

        /// Every selected actor, the primary one last
        const std::vector<std::shared_ptr<Engine::Objects::Actor>>& GetSelectedActors() const {
            return selectedActors;
        }

        size_t GetSelectionCount() const { return selectedActors.size(); }
        bool HasSelection() const { return !selectedActors.empty(); }
        bool IsSelected(const std::shared_ptr<Engine::Objects::Actor>& actor) const;

        /// Replaces the selection (the last of the list becomes the primary actor)
        void SetSelectedActors(std::vector<std::shared_ptr<Engine::Objects::Actor>> actors);
        /// Adds the actor to the selection, as the primary one
        void AddToSelection(const std::shared_ptr<Engine::Objects::Actor>& actor);
        void RemoveFromSelection(const std::shared_ptr<Engine::Objects::Actor>& actor);
        /// Ctrl+click : selects the actor if it was not, deselects it if it was
        void ToggleSelection(const std::shared_ptr<Engine::Objects::Actor>& actor);
        void ClearSelection() { SetSelectedActors({}); }
        void SelectAll();
        /// Every actor of the world : the root ones in the order they were made, each followed by its descendants
        std::vector<std::shared_ptr<Engine::Objects::Actor>> GetAllActors() const;
        void InvertSelection();
        /// Adds the descendants of the selected actors
        void SelectChildren();

        /// Drops from the selection the actors that were destroyed (with their parent, by an undo...), or moves them to the
        /// actors that an undo or a redo rebuilt in their place
        void ValidateSelection();

        // What Edit does on the selection : the menu bar, the outliner and the viewport share it
        void CopySelection();
        void CutSelection();
        void PasteClipboard();
        bool CanPaste() const { return !actorClipboard.IsEmpty(); }
        void DuplicateSelection();
        void DeleteSelection();
        void GroupSelection();
        /// Asks the outliner to rename the primary actor in place
        void RenameSelection();

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
        // Applies a new selection : the outline of the actors that left it is removed, the one of the new ones added
        void ApplySelection(std::vector<std::shared_ptr<Engine::Objects::Actor>> actors);
        void SetOutlined(const std::shared_ptr<Engine::Objects::Actor>& actor, bool outlined);

        // The selected actors, the primary one last
        std::vector<std::shared_ptr<Engine::Objects::Actor>> selectedActors;
        // Their ids, for the panels to ask about every row they draw
        std::unordered_set<int> selectedIds;
        // Follow the selected actors through the undo history, which destroys and rebuilds actors
        std::vector<std::shared_ptr<Editor::Commands::ObjectRef>> selectedRefs;

        // Copy / Cut / Paste
        Commands::ActorClipboard actorClipboard;

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
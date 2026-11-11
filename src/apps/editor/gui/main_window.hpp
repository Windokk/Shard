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
#include "apps/editor/gui/panels/texture_editor/texture_editor_panel.hpp"
#include "apps/editor/gui/panels/asset_editor_registry.hpp"
#include "apps/editor/gui/panels/world_tree/world_tree.hpp"
#include "apps/editor/gui/panels/world_settings/world_settings_panel.hpp"
#include "apps/editor/gui/panels/project_settings/project_settings_panel.hpp"
#include "apps/editor/gui/panels/editor_preferences/editor_preferences_panel.hpp"
#include "apps/editor/gui/panels/console/console.hpp"
#include "apps/editor/gui/panels/profiler/profiler_panel.hpp"
#include "apps/editor/gui/panels/plugins/plugins_panel.hpp"
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

        void PrepareFrame() override;

        void SwapBuffers() override;

        void DrawLoadingFrame(float progress) override;

        int GetFramebufferWidth() const override;

        int GetFramebufferHeight() const override;

        void Destroy() const override;

        bool ShouldClose() const override;
  
        void RequestExit();

        void ProcessInputs() const override;

        void SetSelectedActor(std::shared_ptr<Engine::Objects::Actor> newPtr);

        void RequestPlay();

        bool IsGameView() const { return gameView; }
        void ToggleGameView();

        std::shared_ptr<Engine::Objects::Actor> GetSelectedActor() const {
            return selectedActors.empty() ? nullptr : selectedActors.back();
        }

        const std::vector<std::shared_ptr<Engine::Objects::Actor>>& GetSelectedActors() const {
            return selectedActors;
        }

        size_t GetSelectionCount() const { return selectedActors.size(); }
        bool HasSelection() const { return !selectedActors.empty(); }
        bool IsSelected(const std::shared_ptr<Engine::Objects::Actor>& actor) const;

        void SetSelectedActors(std::vector<std::shared_ptr<Engine::Objects::Actor>> actors);
        void AddToSelection(const std::shared_ptr<Engine::Objects::Actor>& actor);
        void RemoveFromSelection(const std::shared_ptr<Engine::Objects::Actor>& actor);
        void ToggleSelection(const std::shared_ptr<Engine::Objects::Actor>& actor);
        void ClearSelection() { SetSelectedActors({}); }
        void SelectAll();
        std::vector<std::shared_ptr<Engine::Objects::Actor>> GetAllActors() const;
        void InvertSelection();
        void SelectChildren();

        void ValidateSelection();

        void CopySelection();
        void CutSelection();
        void PasteClipboard();
        bool CanPaste() const { return !actorClipboard.IsEmpty(); }
        void DuplicateSelection();
        void DeleteSelection();
        void GroupSelection();
        void RenameSelection();

        Engine::Core::Platform::SystemInfos GetSystemInfos() const override;

        
        GUI::ViewportWindow* viewport = nullptr;

        EditorPreferences preferences;
        EditorSettings& settings = preferences.viewport;
        PanelVisibility& panelVisibility = preferences.panels;

        void ApplyAppearance();

    protected:
        void OnNativeEvent(const SDL_Event& event) override;

    private:
        void EnsureImGuiInitialized();
        void DrawLoadingOverlay(float progress);

        void OnWorldLoaded();

        void UpdateProbeBuildNotification();
        void UpdateLightmapBuildNotification();

        void UpdatePreferences();
        void UpdateAutoSave();

        bool HasUnsavedProjectSettings() const;
        void UpdateCloseRequest();

        bool imguiInitialized = false;
        bool renderPassesInitialized = false;
        bool gameView = false;

        GUI::AssetBrowser* assetBrowser = nullptr;
        GUI::MaterialEditorPanel* materialEditorPanel = nullptr;
        GUI::TextureEditorPanel* textureEditorPanel = nullptr;
        GUI::PropertiesPanel* propertiesPanel = nullptr;
        GUI::WorldTree* worldTree = nullptr;
        GUI::WorldSettingsPanel* worldSettingsPanel = nullptr;
        GUI::ProjectSettingsPanel* projectSettingsPanel = nullptr;
        GUI::EditorPreferencesPanel* editorPreferencesPanel = nullptr;
        GUI::Console* console = nullptr;
        GUI::ProfilerPanel* profilerPanel = nullptr;
        GUI::PluginsPanel* pluginsPanel = nullptr;
        GUI::MenuBar* menuBar = nullptr;
        
        Debug::EditorDebugDraw debugDraw;

        void ApplySelection(std::vector<std::shared_ptr<Engine::Objects::Actor>> actors);
        void SetOutlined(const std::shared_ptr<Engine::Objects::Actor>& actor, bool outlined);

        std::vector<std::shared_ptr<Engine::Objects::Actor>> selectedActors;
        std::unordered_set<int> selectedIds;
        std::vector<std::shared_ptr<Editor::Commands::ObjectRef>> selectedRefs;

        Commands::ActorClipboard actorClipboard;

        std::string savedPreferencesText;
        double nextPreferencesCheck = 0.0;
        AppearancePreferences appliedAppearance;
        double lastAutoSaveTime = 0.0;

        bool closeConfirmed = false;

        std::vector<std::pair<std::string, std::shared_ptr<Engine::Rendering::Material>>> outlinePipelineFullscreenCommands;

        std::vector<std::pair<std::string, std::shared_ptr<Engine::Rendering::Material>>> gridCommands;
    };
}
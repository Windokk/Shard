#pragma once

#include "engine/platform/windowing/iwindow.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/world/actor.hpp"

#include "engine/renderer/rhi/backends/glad/include/glad/gl.h"
#include "engine/renderer/rhi/backends/glad/include/glad/vulkan.h"
#include "engine/platform/windowing/sdl/sdl_window.hpp"
#include "apps/editor/gui/panels/viewport/viewport_window.hpp"
#include "apps/editor/gui/panels/properties/properties_panel.hpp"
#include "apps/editor/gui/panels/asset_browser/asset_browser.hpp"
#include "apps/editor/gui/panels/material_editor/material_editor_panel.hpp"
#include "apps/editor/gui/panels/asset_editor_registry.hpp"
#include "apps/editor/gui/panels/world_tree/world_tree.hpp"
#include "apps/editor/gui/panels/world_settings/world_settings_panel.hpp"
#include "apps/editor/gui/panels/project_settings/project_settings_panel.hpp"
#include "apps/editor/gui/panels/console/console.hpp"
#include "apps/editor/gui/panels/profiler/profiler_panel.hpp"
#include "apps/editor/gui/panels/menu_bar/menu_bar.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "imgui/backends/imgui_impl_vulkan.h"

#include <cstdint>

namespace Shard::Editor::Core {

    enum EditorViewportBuffer{
        Final,
        Shadows,
        ObjectID,
        MatID
    };

    struct EditorSettings{

        // Viewport settings
        bool showOutlines = true;
        bool showGizmos = true;
        bool showDDGIGizmos = true;
        bool showPhysicsShapes = true;
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

        /// TODO
        /// bool showBillboards;
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
  
        void RequestExit();

        void ProcessInputs() const override;

        void SetSelectedActor(std::shared_ptr<Engine::Objects::Actor> newPtr);

        std::shared_ptr<Engine::Objects::Actor> GetSelectedActor(){
            return selectedActor;
        }

        Engine::Core::Platform::SystemInfos GetSystemInfos() const override;

        
        GUI::ViewportWindow* viewport = nullptr;

        EditorSettings settings;
        PanelVisibility panelVisibility;

    protected:
        // ImGui reads the SDL events itself
        void OnNativeEvent(const SDL_Event& event) override;

    private:
        void EnsureImGuiInitialized();
        void DrawLoadingOverlay(float progress);

        // Polls the ProbeManager's async scene-build progress and mirrors it into a single
        // "Baking GI probes" progress notification (see editor/gui/notifications.hpp).
        void UpdateProbeBuildNotification();

        bool imguiInitialized = false;
        bool renderPassesInitialized = false;

        // Panels
        GUI::AssetBrowser* assetBrowser = nullptr;
        GUI::MaterialEditorPanel* materialEditorPanel = nullptr;
        GUI::PropertiesPanel* propertiesPanel = nullptr;
        GUI::WorldTree* worldTree = nullptr;
        GUI::WorldSettingsPanel* worldSettingsPanel = nullptr;
        GUI::ProjectSettingsPanel* projectSettingsPanel = nullptr;
        GUI::Console* console = nullptr;
        GUI::ProfilerPanel* profilerPanel = nullptr;
        GUI::MenuBar* menuBar = nullptr;
        
        // User data
        std::shared_ptr<Engine::Objects::Actor> selectedActor = nullptr;

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
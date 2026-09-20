#pragma once

#include "engine/core/platform/iwindow.hpp"
#include "engine/rendering/renderer/renderer.hpp"
#include "engine/rendering/material/material.hpp"
#include "engine/debugging/logger.hpp"
#include "engine/objects/actors/actor.hpp"

#include "editor/core/platform/glfw/glfw_input.hpp"
#include "editor/gui/panels/viewport/viewport_window.hpp"
#include "editor/gui/panels/properties/properties_panel.hpp"
#include "editor/gui/panels/asset_browser/asset_browser.hpp"
#include "editor/gui/panels/material_editor/material_editor_panel.hpp"
#include "editor/gui/panels/asset_editor_registry.hpp"
#include "editor/gui/panels/level_tree/level_tree.hpp"
#include "editor/gui/panels/level_settings/level_settings_panel.hpp"
#include "editor/gui/panels/project_settings/project_settings_panel.hpp"
#include "editor/gui/panels/console/console.hpp"
#include "editor/gui/panels/profiler/profiler_panel.hpp"
#include "editor/gui/panels/menu_bar/menu_bar.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/backends/imgui_impl_glfw.h"
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
        bool levelTree = true;
        bool properties = true;
        bool assetBrowser = true;
        bool console = true;
        bool profiler = true;
        bool levelSettings = true;
        bool projectSettings = false;
    };

    class EditorMainWindow : public Engine::Core::Platform::IWindow {
    public:
        void Init(const std::string& title, const int& width, const int& height, 
                    const bool& fullscreen, const int& vsync, const uint32_t& api) override;

        void SetGLFWInputManager(GLFWInput* inputManager);

        void SetTitle(const std::string& title) override;

        void PollEvents() override;

        void SwapBuffers() override;

        void DrawLoadingFrame(float progress) override;

        bool ShouldClose() const override;

        int GetFramebufferWidth() const override;

        int GetFramebufferHeight() const override;

        void* GetNativeHandle() const override;

        void Destroy() const override;
  
        void ToggleFullscreen() override;

        void RequestExit();

        void ProcessInputs() const override;

        int GetBytesPerPixel() const override;

        void SetSelectedActor(std::shared_ptr<Engine::Objects::Actor> newPtr);

        std::shared_ptr<Engine::Objects::Actor> GetSelectedActor(){
            return selectedActor;
        }

        Engine::Core::Platform::SystemInfos GetSystemInfos() const override;

        GLFWInput* inputManager;
        
        GUI::ViewportWindow* viewport = nullptr;

        EditorSettings settings;
        PanelVisibility panelVisibility;

    private:
        void EnsureImGuiInitialized();
        void DrawLoadingOverlay(float progress);

        // Polls the ProbeManager's async scene-build progress and mirrors it into a single
        // "Baking GI probes" progress notification (see editor/gui/notifications.hpp).
        void UpdateProbeBuildNotification();

        bool imguiInitialized = false;
        bool renderPassesInitialized = false;

        GLFWwindow* window = nullptr;

        int windowPosX = 0;
        int windowPosY = 0;
        int windowWidth = 0;
        int windowHeight = 0;

        // Panels
        GUI::AssetBrowser* assetBrowser = nullptr;
        GUI::MaterialEditorPanel* materialEditorPanel = nullptr;
        GUI::PropertiesPanel* propertiesPanel = nullptr;
        GUI::LevelTree* levelTree = nullptr;
        GUI::LevelSettingsPanel* levelSettingsPanel = nullptr;
        GUI::ProjectSettingsPanel* projectSettingsPanel = nullptr;
        GUI::Console* console = nullptr;
        GUI::ProfilerPanel* profilerPanel = nullptr;
        GUI::MenuBar* menuBar = nullptr;
        
        // User data
        std::shared_ptr<Engine::Objects::Actor> selectedActor = nullptr;

        // The JFA (init/step) and outline composite passes each only ever hold a single, permanent
        // fullscreen-triangle draw command (see the render pass setup in SwapBuffers) - a level reload
        // (Renderer::ClearPassesContent(), see EngineInstance::Run()'s m_ReloadCurrentLevel handling)
        // wipes every pass's draw list, and since that setup only ever runs once, those commands would
        // otherwise never come back. Re-submitted every frame in SwapBuffers alongside the selected
        // actor's mask commands so the outline survives any such reload.
        std::vector<std::pair<std::string, std::shared_ptr<Engine::Rendering::Material>>> outlinePipelineFullscreenCommands;
    };
}
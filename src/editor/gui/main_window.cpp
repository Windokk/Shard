#include "main_window.hpp"

#include "engine/core/engine.hpp"

#include "editor/gui/panels/asset_browser/asset_browser.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>

#include "editor/gui/IconsLucide.h"
#include "editor/gui/notifications.hpp"
#include "editor/gui/popups.hpp"
#include "editor/gui/loading_widgets.hpp"

#include "engine/rendering/lighting/probe_manager.hpp"

#include "engine/core/resources/resources_manager.hpp"
#include "engine/levels/level_manager.hpp"
#include "engine/projects/project.hpp"
#include "engine/rendering/renderer/renderer.hpp"
#include "engine/rendering/shader/shader.hpp"
#include "engine/rendering/pipeline/pipeline.hpp"
#include "engine/rendering/material/material.hpp"
#include "engine/objects/components/rendering/model_component.hpp"
#include "engine/core/engine.hpp"

namespace Shard::Editor::Core{

    void SetupImGuiStyle()
    {
        // Shard style from ImThemes
        ImGuiStyle& style = ImGui::GetStyle();
        
        style.Alpha = 1.0f;
        style.DisabledAlpha = 0.6f;
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.WindowRounding = 0.0f;
        style.WindowBorderSize = 1.0f;
        style.WindowMinSize = ImVec2(32.0f, 32.0f);
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_Left;
        style.ChildRounding = 0.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupRounding = 0.0f;
        style.PopupBorderSize = 1.0f;
        style.FramePadding = ImVec2(4.0f, 4.0f);
        style.FrameRounding = 4.0f;
        style.FrameBorderSize = 0.0f;
        style.ItemSpacing = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
        style.CellPadding = ImVec2(4.0f, 2.0f);
        style.IndentSpacing = 21.0f;
        style.ColumnsMinSpacing = 6.0f;
        style.ScrollbarSize = 14.0f;
        style.ScrollbarRounding = 9.0f;
        style.GrabMinSize = 10.0f;
        style.GrabRounding = 0.0f;
        style.TabRounding = 4.0f;
        style.TabBorderSize = 0.0f;
        style.ColorButtonPosition = ImGuiDir_Right;
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign = ImVec2(0.5f, 0.0f);
        
        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
        colors[ImGuiCol_ChildBg]                = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
        colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
        colors[ImGuiCol_Border]                 = ImVec4(0.36f, 0.43f, 0.53f, 0.50f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.20f, 0.21f, 0.22f, 0.54f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.33f, 0.49f, 0.60f, 0.62f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.18f, 0.18f, 0.18f, 0.67f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.29f, 0.29f, 0.29f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.34f, 0.42f, 0.48f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.33f, 0.49f, 0.60f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.44f, 0.44f, 0.44f, 0.40f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.33f, 0.49f, 0.60f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.35f, 0.35f, 0.35f, 0.31f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.25f, 0.34f, 0.40f, 0.80f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.48f, 0.50f, 0.52f, 1.00f);
        colors[ImGuiCol_Separator]              = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.72f, 0.72f, 0.72f, 0.78f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.91f, 0.91f, 0.91f, 0.25f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.81f, 0.81f, 0.81f, 0.67f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.46f, 0.46f, 0.46f, 0.95f);
        colors[ImGuiCol_InputTextCursor]        = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.28f, 0.34f, 0.40f, 1.00f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.20f, 0.26f, 0.31f, 0.86f);
        colors[ImGuiCol_TabSelected]            = ImVec4(0.33f, 0.49f, 0.60f, 1.00f);
        colors[ImGuiCol_TabSelectedOverline]    = ImVec4(0.31f, 0.37f, 0.45f, 1.00f);
        colors[ImGuiCol_TabDimmed]              = ImVec4(0.15f, 0.20f, 0.25f, 0.97f);
        colors[ImGuiCol_TabDimmedSelected]      = ImVec4(0.23f, 0.26f, 0.30f, 1.00f);
        colors[ImGuiCol_TabDimmedSelectedOverline]  = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
        colors[ImGuiCol_DockingPreview]         = ImVec4(0.20f, 0.26f, 0.31f, 0.86f);
        colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_PlotLines]              = ImVec4(0.53f, 0.53f, 0.53f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.65f, 0.79f, 0.88f, 1.00f);
        colors[ImGuiCol_PlotHistogram]          = ImVec4(0.36f, 0.57f, 0.70f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.61f, 0.79f, 0.90f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.41f, 0.47f, 0.50f, 1.00f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
        colors[ImGuiCol_TextLink]               = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.87f, 0.87f, 0.87f, 0.35f);
        colors[ImGuiCol_TreeLines]              = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
        colors[ImGuiCol_DragDropTarget]         = ImVec4(0.33f, 0.49f, 0.60f, 1.00f);
        colors[ImGuiCol_DragDropTargetBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_UnsavedMarker]          = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_NavCursor]              = ImVec4(0.39f, 0.61f, 0.78f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.7f);
        colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    }

    ImFont* LoadFontFromFile(const std::string& filePath, float fontSize, bool icons)
    {
        // Open the file in binary mode
        std::ifstream fontFile(filePath, std::ios::binary | std::ios::ate);
        if (!fontFile.is_open())
        {
            DEBUG_ERROR("Failed to load editor font: %s\n", filePath.c_str());
            return nullptr;
        }

        // Get file size and read content
        std::streamsize size = fontFile.tellg();
        fontFile.seekg(0, std::ios::beg);

        if (size <= 0)
        {
            DEBUG_ERROR("Failed to load editor font (empty file): %s\n", filePath.c_str());
            return nullptr;
        }

        std::vector<unsigned char> fontData(size);
        if (!fontFile.read(reinterpret_cast<char*>(fontData.data()), size))
        {
            DEBUG_ERROR("Failed to read editor font: %s\n", filePath.c_str());
            return nullptr;
        }

        fontFile.close();

        // Allocate memory ImGui can take ownership of
        void* fontMemory = malloc(fontData.size());
        if (!fontMemory)
            return nullptr;

        memcpy(fontMemory, fontData.data(), fontData.size());

        ImGuiIO& io = ImGui::GetIO();

        ImFontConfig cfg{};
        cfg.MergeMode = icons;
        cfg.FontDataOwnedByAtlas = true;
        cfg.PixelSnapH = true;
        cfg.GlyphOffset.y = icons ? 3.0f : 0.0f;

        const ImWchar icons_ranges[] = { ICON_MIN_LC, ICON_MAX_16_LC, 0 };

        ImFont* font = nullptr;
        if (icons)
        {
            font = io.Fonts->AddFontFromMemoryTTF(
                fontMemory,
                static_cast<int>(fontData.size()),
                fontSize,
                &cfg,
                icons_ranges
            );
        }
        else
        {
            font = io.Fonts->AddFontFromMemoryTTF(
                fontMemory,
                static_cast<int>(fontData.size()),
                fontSize,
                &cfg,
                io.Fonts->GetGlyphRangesDefault()
            );
        }

        if (!font)
        {
            DEBUG_ERROR("Failed to load editor font from ImGui memory: %s\n", filePath.c_str());
            free(fontMemory);
        }

        return font;
    }

    void EditorMainWindow::SetSelectedActor(std::shared_ptr<Engine::Objects::Actor> newPtr)
    {
        // EditorOutlineMaskPass only ever contains the currently selected actor's own model(s) (see the
        // matching comment where the pass is created in SwapBuffers) - swapping the selection means
        // pulling the old actor's draw commands out and pushing the new one's in, rather than drawing
        // the whole scene and sorting it out per-fragment with an objID compare.
        if(renderPassesInitialized && selectedActor)
            for(auto& comp : selectedActor->GetComponents())
                if(auto model = std::dynamic_pointer_cast<Engine::Objects::Components::Model>(comp))
                    model->RemoveFromPass("EditorOutlineMaskPass");

        this->selectedActor = newPtr;

        if(renderPassesInitialized && selectedActor)
            for(auto& comp : selectedActor->GetComponents())
                if(auto model = std::dynamic_pointer_cast<Engine::Objects::Components::Model>(comp))
                    model->AddToPass("EditorOutlineMaskPass");

        if(levelTree)
            levelTree->SetSelection(newPtr);
    }

    void EditorMainWindow::Init(const std::string &title, const int &width, const int &height, const bool &fullscreen, const int &vsync, const uint32_t& api)
    {
        //Init glfw and gl context
        glfwInit();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_SAMPLES, 4);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        if(fullscreen){
            GLFWmonitor* primary = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(primary);
            window = glfwCreateWindow(mode->width, mode->height, title.c_str(), primary, nullptr);
        }
        else{
            window = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
        }
        
        if (window == NULL)
        {
            glfwTerminate();
            DEBUG_FATAL("Failed to create GLFW window");
        }

        glfwMakeContextCurrent(window);
        glfwSetWindowUserPointer(window, this);
        glfwSwapInterval(vsync);

        if(api == (uint32_t)Engine::Rendering::RendererAPI::API::OpenGL)
            gladLoadGL((GLADloadfunc)glfwGetProcAddress);
        else if(api == (uint32_t)Engine::Rendering::RendererAPI::API::Vulkan)
        {    
            //gladLoadVulkan(Engine::Core::GetEngine().GetRenderer()->GetDevicePointer?,(GLADloadfunc)glfwGetProcAddress);
        }
    }

    void EditorMainWindow::SetGLFWInputManager(GLFWInput *inputManager)
    {
        this->inputManager = inputManager;
    }

    void EditorMainWindow::SetTitle(const std::string &title)
    {
        glfwSetWindowTitle(window, title.c_str());
    }

    void EditorMainWindow::PollEvents()
    {
        glfwPollEvents();
    }

    void EditorMainWindow::EnsureImGuiInitialized()
    {
        if(imguiInitialized)
            return;

        //Init ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;
        io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports;

        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init();

        SetupImGuiStyle();

        LoadFontFromFile("editor_resources/fonts/OpenSans-Regular.ttf", 16, false);
        LoadFontFromFile("editor_resources/fonts/lucide.ttf", 16, true);

        //Init Panels
        assetBrowser = new GUI::AssetBrowser();
        assetBrowser->NavigateTo(Engine::Core::GetEngine().GetCurrentProject()->GetProjectResourcesPath().full);
        assetBrowser->SetParentWindow(this);
        materialEditorPanel = new GUI::MaterialEditorPanel();
        GUI::AssetEditorRegistry::Instance().Register(Engine::Filesystem::Type::T_MATERIAL, materialEditorPanel);
        propertiesPanel = new GUI::PropertiesPanel();
        levelTree = new GUI::LevelTree();
        levelTree->SetParentWindow(this);
        levelSettingsPanel = new GUI::LevelSettingsPanel();
        viewport = new GUI::ViewportWindow();
        viewport->SetParentWindow(this);
        console = new GUI::Console();
        console->SetParentWindow(this);
        profilerPanel = new GUI::ProfilerPanel();
        menuBar = new GUI::MenuBar();
        menuBar->SetParentWindow(this);

        GUI::EditorResources::Instance().Init();

        imguiInitialized = true;
    }

    void EditorMainWindow::DrawLoadingOverlay(float progress)
    {
        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x + mainViewport->WorkSize.x * 0.5f,
                                        mainViewport->WorkPos.y + mainViewport->WorkSize.y * 0.5f),
                                        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        // Fully opaque - this overlay stands in for the whole editor while a level loads, so letting the
        // still-mid-initialization viewport/panels show through underneath it (the old 0.85 alpha) read
        // as a rendering glitch rather than a deliberate loading screen.
        ImGui::SetNextWindowBgAlpha(1.0f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_AlwaysAutoResize;

        // A dedicated, slightly-elevated background (rather than the default ImGuiCol_WindowBg) plus
        // rounded corners and generous padding, so this reads as a purpose-built loading card and not a
        // resized debug window.
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.10f, 0.11f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.34f, 0.38f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32.0f, 26.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);

        if(ImGui::Begin("##LoadingLevelOverlay", nullptr, flags))
        {
            // Matches the editor's own blue-teal accent (see ImGuiCol_SliderGrabActive/ButtonHovered in
            // SetupImGuiStyle) so this overlay doesn't look like it belongs to a different app.
            const ImU32 accent = IM_COL32(84, 125, 153, 255);
            const ImU32 accentBright = IM_COL32(130, 178, 212, 255);
            constexpr float barWidth = 280.0f;
            constexpr float spinnerRadius = 14.0f;

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            ImVec2 spinnerTopLeft = ImGui::GetCursorScreenPos();
            ImVec2 spinnerCentre(spinnerTopLeft.x + barWidth * 0.5f, spinnerTopLeft.y + spinnerRadius);
            GUI::LoadingWidgets::SpinnerFadePulsar(drawList, spinnerCentre, spinnerRadius, accent, 1.8f, 2);
            ImGui::Dummy(ImVec2(barWidth, spinnerRadius * 2.0f + 14.0f));

            const char* label = "Loading Level...";
            float labelWidth = ImGui::CalcTextSize(label).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (barWidth - labelWidth) * 0.5f);
            ImGui::TextUnformatted(label);

            ImGui::Dummy(ImVec2(0.0f, 8.0f));

            ImVec2 barPos = ImGui::GetCursorScreenPos();
            ImVec2 barSize(barWidth, 14.0f);
            GUI::LoadingWidgets::DrawProgressBar(drawList, barPos, barSize, progress,
                IM_COL32(20, 21, 23, 255), accent, IM_COL32(60, 68, 76, 255));
            ImGui::Dummy(barSize);

            std::string percentLabel = std::to_string((int)std::lround(std::clamp(progress, 0.0f, 1.0f) * 100.0f)) + "%";
            float percentWidth = ImGui::CalcTextSize(percentLabel.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (barWidth - percentWidth) * 0.5f);
            ImGui::TextDisabled("%s", percentLabel.c_str());
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }

    void EditorMainWindow::DrawLoadingFrame(float progress)
    {
        EnsureImGuiInitialized();

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        DrawLoadingOverlay(progress);

        GUI::Notifications::RenderFrame();
        GUI::Popups::Draw();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    void EditorMainWindow::SwapBuffers()
    {
        EnsureImGuiInitialized();

        Rendering::Renderer* renderer = Engine::Core::GetEngine().GetRenderer();

        // Wait for viewport size to be initialized
        if(!renderPassesInitialized && viewport->GetViewportSize() != glm::vec2(50,50)
            && viewport->GetViewportSize().x > 0 && viewport->GetViewportSize().y > 0){
        
            /// Outline mask pass

            Rendering::FramebufferSpecifications fbOutlineMaskSpecs;
            fbOutlineMaskSpecs.hasColor = true;
            fbOutlineMaskSpecs.hasDepth = true;
            fbOutlineMaskSpecs.colorSpecs = {};
            fbOutlineMaskSpecs.width = viewport->GetViewportSize().x;
            fbOutlineMaskSpecs.height = viewport->GetViewportSize().y;

            std::shared_ptr<Rendering::Framebuffer> fbOutlineMask = Rendering::Framebuffer::Create(fbOutlineMaskSpecs);

            std::shared_ptr<Rendering::Shader> outlineMaskShader = Engine::Core::GetEngine().GetResourcesManager()->GetShader("shaders/editor/outline_mask");
            
            Rendering::PipelineSpecifications outlineMaskPipelineSpecs;
            outlineMaskPipelineSpecs.shader = outlineMaskShader;
            outlineMaskPipelineSpecs.debugName = "OutlineMaskPipeline";
            std::shared_ptr<Rendering::Pipeline> outlineMaskPipeline = Engine::Core::GetEngine().GetRenderer()->GetOrAddPipeline(outlineMaskPipelineSpecs);

            std::shared_ptr<Rendering::Material> outlineMaskMaterial = Rendering::Material::Create(outlineMaskShader, outlineMaskPipeline, false, Rendering::Opacity::Opaque);

            std::shared_ptr<Rendering::RenderPass> outlineMaskPass = std::make_shared<Rendering::RenderPass>();
            outlineMaskPass->clearColor = true;
            outlineMaskPass->clearDepth = true;
            outlineMaskPass->customPipeline = outlineMaskPipeline;
            outlineMaskPass->target = fbOutlineMask;
            outlineMaskPass->overridePipeline = true;

            renderer->AddRenderPass(outlineMaskPass, "EditorOutlineMaskPass", {});

            // Only the selected actor's own model(s) ever get registered into this pass (see
            // SetSelectedActor) - the mask shader can just paint white unconditionally instead of
            // comparing objID, and the pass only ever rasterizes the one object that matters instead of
            // the whole scene. If a selection was already made before this pass existed (e.g. selecting
            // something while the viewport panel was still settling its size), register it retroactively.
            if(selectedActor)
                for(auto& comp : selectedActor->GetComponents())
                    if(auto model = std::dynamic_pointer_cast<Engine::Objects::Components::Model>(comp))
                        model->AddToPass("EditorOutlineMaskPass");

            /////////

            /// Jump Flood Algorithm - propagates, for every pixel, the position of its nearest
            /// silhouette (mask) pixel, so the final pass can turn that into a true Euclidean distance
            /// field instead of the old fixed 3x3-neighbor mask check (which only ever caught
            /// 1px-aligned edges and gave an outline whose thickness varied with edge angle).
            /// Ping-pongs between two RGBA32F buffers: .xy holds the seed's pixel position, .z is a
            /// validity flag (1.0 = has a seed) rather than a magic sentinel coordinate.

            Rendering::FramebufferSpecifications fbJFASpecs;
            fbJFASpecs.hasColor = true;
            fbJFASpecs.hasDepth = false;
            fbJFASpecs.width = viewport->GetViewportSize().x;
            fbJFASpecs.height = viewport->GetViewportSize().y;
            fbJFASpecs.colorSpecs.internalFormat = Rendering::TextureInternalFormat::RGBA32F;
            fbJFASpecs.colorSpecs.minFilter = Rendering::TextureFilter::Nearest;
            fbJFASpecs.colorSpecs.magFilter = Rendering::TextureFilter::Nearest;
            fbJFASpecs.colorSpecs.wrapS = Rendering::TextureWrap::ClampEdge;
            fbJFASpecs.colorSpecs.wrapT = Rendering::TextureWrap::ClampEdge;
            fbJFASpecs.colorSpecs.generateMips = false;

            std::shared_ptr<Rendering::Framebuffer> fbJFA_A = Rendering::Framebuffer::Create(fbJFASpecs);
            std::shared_ptr<Rendering::Framebuffer> fbJFA_B = Rendering::Framebuffer::Create(fbJFASpecs);

            glm::vec2 jfaTexelSize = glm::vec2(1.0f / fbJFASpecs.width, 1.0f / fbJFASpecs.height);

            Rendering::PipelineSpecifications jfaInitPipelineSpecs;
            jfaInitPipelineSpecs.depthTest = false;
            jfaInitPipelineSpecs.depthWrite = false;
            jfaInitPipelineSpecs.blending = false;
            jfaInitPipelineSpecs.shader = Engine::Core::GetEngine().GetResourcesManager()->GetShader("shaders/editor/jfa_init");
            jfaInitPipelineSpecs.debugName = "JFAInitPipeline";
            jfaInitPipelineSpecs.vertexLayout = {};
            std::shared_ptr<Rendering::Pipeline> jfaInitPipeline = renderer->GetOrAddPipeline(jfaInitPipelineSpecs);
            std::shared_ptr<Rendering::Material> jfaInitMaterial = Rendering::Material::Create(jfaInitPipelineSpecs.shader, jfaInitPipeline, false, Rendering::Opacity::Opaque);

            std::shared_ptr<Rendering::RenderPass> jfaInitPass = std::make_shared<Rendering::RenderPass>();
            jfaInitPass->clearColor = true;
            jfaInitPass->customPipeline = jfaInitPipeline;
            jfaInitPass->target = fbJFA_A;
            jfaInitPass->overridePipeline = true;
            jfaInitPass->customSamplers["maskTex"] = fbOutlineMask->GetColorAttachment();
            renderer->AddRenderPass(jfaInitPass, "EditorJFAInitPass", {"EditorOutlineMaskPass"});

            Rendering::DrawCommand jfaInitCmd{};
            jfaInitCmd.fullscreenTri = true;
            jfaInitCmd.bindCameraState = false;
            jfaInitCmd.material = jfaInitMaterial;
            renderer->AddOrUpdateCommands({jfaInitCmd}, {"EditorJFAInitPass"}, false);
            outlinePipelineFullscreenCommands.push_back({"EditorJFAInitPass", jfaInitMaterial});

            Rendering::PipelineSpecifications jfaStepPipelineSpecs;
            jfaStepPipelineSpecs.depthTest = false;
            jfaStepPipelineSpecs.depthWrite = false;
            jfaStepPipelineSpecs.blending = false;
            jfaStepPipelineSpecs.shader = Engine::Core::GetEngine().GetResourcesManager()->GetShader("shaders/editor/jfa_step");
            jfaStepPipelineSpecs.debugName = "JFAStepPipeline";
            jfaStepPipelineSpecs.vertexLayout = {};
            std::shared_ptr<Rendering::Pipeline> jfaStepPipeline = renderer->GetOrAddPipeline(jfaStepPipelineSpecs);

            // Fixed falling step sequence rather than the full log2(max(width,height)) chain a
            // whole-image distance transform would need - correct for any pixel within ~31px of a
            // seed, which is far more than a selection outline ever needs (outlineThickness below).
            const float steps[] = {16.0f, 8.0f, 4.0f, 2.0f, 1.0f};
            std::shared_ptr<Rendering::Framebuffer> jfaRead = fbJFA_A;
            std::shared_ptr<Rendering::Framebuffer> jfaWrite = fbJFA_B;
            std::string prevPassName = "EditorJFAInitPass";

            for (int i = 0; i < 5; i++)
            {
                std::shared_ptr<Rendering::Material> stepMaterial = Rendering::Material::Create(jfaStepPipelineSpecs.shader, jfaStepPipeline, false, Rendering::Opacity::Opaque);

                std::shared_ptr<Rendering::RenderPass> stepPass = std::make_shared<Rendering::RenderPass>();
                stepPass->clearColor = true;
                stepPass->customPipeline = jfaStepPipeline;
                stepPass->target = jfaWrite;
                stepPass->overridePipeline = true;
                stepPass->customUniforms["stepSize"] = steps[i];
                stepPass->customUniforms["texelSize"] = jfaTexelSize;
                stepPass->customSamplers["seedTex"] = jfaRead->GetColorAttachment();

                std::string passName = "EditorJFAStepPass" + std::to_string(i);
                renderer->AddRenderPass(stepPass, passName, {prevPassName});

                Rendering::DrawCommand stepCmd{};
                stepCmd.fullscreenTri = true;
                stepCmd.bindCameraState = false;
                stepCmd.material = stepMaterial;
                renderer->AddOrUpdateCommands({stepCmd}, {passName}, false);
                outlinePipelineFullscreenCommands.push_back({passName, stepMaterial});

                prevPassName = passName;
                std::swap(jfaRead, jfaWrite);
            }

            // After an odd number of steps (5), the final result sits in jfaRead (the chain swaps
            // read/write at the end of every iteration above).
            std::shared_ptr<Rendering::Framebuffer> fbJFAFinal = jfaRead;

            /////////

            /// Outline Pass - composited straight onto the already-shaded scene (the viewport
            /// framebuffer), right after everything else has drawn into it, so the mask's edge pixels
            /// (the only ones outline.frag doesn't discard) land on top of the final image instead of
            /// into a framebuffer nothing ever samples.

            std::shared_ptr<Rendering::Shader> outlineShader = Engine::Core::GetEngine().GetResourcesManager()->GetShader("shaders/editor/outline");

            Rendering::PipelineSpecifications outlinePipelineSpecs;
            outlinePipelineSpecs.depthTest = false;
            outlinePipelineSpecs.depthWrite = false;
            outlinePipelineSpecs.blending = false;
            outlinePipelineSpecs.shader = outlineShader;
            outlinePipelineSpecs.debugName = "FullscreenOutlinePipeline";
            outlinePipelineSpecs.vertexLayout = {};
            std::shared_ptr<Rendering::Pipeline> outlinePipeline = Engine::Core::GetEngine().GetRenderer()->GetOrAddPipeline(outlinePipelineSpecs);

            std::shared_ptr<Rendering::Material> outlineMaterial = Rendering::Material::Create(outlineShader, outlinePipeline, false, Rendering::Opacity::Opaque);

            std::shared_ptr<Rendering::RenderPass> outlinePass = std::make_shared<Rendering::RenderPass>();
            outlinePass->clearColor = false;
            outlinePass->clearDepth = false;
            outlinePass->customPipeline = outlinePipeline;
            outlinePass->target = renderer->GetViewportFramebuffer();
            outlinePass->overridePipeline = true;
            outlinePass->customUniforms["outlineThickness"] = 4.0f;
            outlinePass->customUniforms["outlineColor"] = glm::vec3(1.0f, 0.722f, 0.0f);
            outlinePass->customSamplers["maskTex"] = fbOutlineMask->GetColorAttachment();
            outlinePass->customSamplers["seedTex"] = fbJFAFinal->GetColorAttachment();

            renderer->AddRenderPass(outlinePass, "EditorOutlinePass", {prevPassName, "ProbeGizmoPass"});

            Rendering::DrawCommand cmd{};
            cmd.fullscreenTri = true;
            cmd.bindCameraState = false;
            cmd.material = outlineMaterial;

            renderer->AddOrUpdateCommands({cmd}, {"EditorOutlinePass"}, false);
            outlinePipelineFullscreenCommands.push_back({"EditorOutlinePass", outlineMaterial});

            renderPassesInitialized = true;
        }

        if(renderPassesInitialized)
        {
            auto outlineMaskPass = renderer->GetRenderPass("EditorOutlineMaskPass");
            auto outlinePass = renderer->GetRenderPass("EditorOutlinePass");
            outlineMaskPass->enabled = settings.showOutlines;
            outlinePass->enabled = settings.showOutlines;

            Rendering::DebugViewState debugView;
            debugView.mode = settings.viewMode;
            debugView.showLighting = settings.showLighting;
            debugView.showShadows = settings.showShadows;
            renderer->SetDebugView(debugView);

            // Re-submit the selected actor's model(s) every frame rather than only on selection change -
            // AddToPass captures the transform's current matrix at call time (see
            // Mesh::CreateDrawCommands), so without this the mask would keep drawing the object at
            // whatever position/rotation/scale it had at the moment it got selected, going stale the
            // instant it's moved by a gizmo drag, a script, or physics. AddOrUpdateCommands treats this
            // as a cheap update-in-place (matching commandID) rather than a fresh registration.
            if(selectedActor)
                for(auto& comp : selectedActor->GetComponents())
                    if(auto model = std::dynamic_pointer_cast<Engine::Objects::Components::Model>(comp))
                        model->AddToPass("EditorOutlineMaskPass");

            // Same reasoning as above, but for the JFA/composite passes' single permanent fullscreen
            // command each (see where outlinePipelineFullscreenCommands is populated in the one-time
            // setup above) - a level reload clears every pass's draw list (ClearPassesContent), and
            // that setup never runs again, so without this the outline silhouette mask would keep
            // updating correctly on selection while nothing ever turns it into a visible outline again.
            for(auto& [passName, material] : outlinePipelineFullscreenCommands)
            {
                Rendering::DrawCommand fsCmd{};
                fsCmd.fullscreenTri = true;
                fsCmd.bindCameraState = false;
                fsCmd.material = material;
                renderer->AddOrUpdateCommands({fsCmd}, {passName}, false);
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();


        ImGuizmo::BeginFrame();

        menuBar->Draw();

        ImGui::DockSpaceOverViewport();

        if(panelVisibility.assetBrowser)
            assetBrowser->Draw();
        if(panelVisibility.viewport)
            viewport->Draw();
        if(panelVisibility.properties)
            propertiesPanel->Draw(selectedActor);
        if(panelVisibility.levelTree)
            levelTree->Draw();
        if(panelVisibility.levelSettings)
            levelSettingsPanel->Draw();
        if(panelVisibility.console)
            console->Draw();
        if(panelVisibility.profiler)
            profilerPanel->Draw();

        materialEditorPanel->Draw();

        if(Engine::Core::GetEngine().GetLevelManager()->IsAsyncLoadInProgress())
            DrawLoadingOverlay(Engine::Core::GetEngine().GetLevelManager()->GetAsyncLoadProgress());

        UpdateProbeBuildNotification();
        GUI::Notifications::RenderFrame();
        GUI::Popups::Draw();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    void EditorMainWindow::UpdateProbeBuildNotification()
    {
        Rendering::Renderer* renderer = Engine::Core::GetEngine().GetRenderer();
        if(!renderer)
            return;

        auto probeManager = renderer->GetProbeManager();
        if(!probeManager)
            return;

        // One long-lived progress notification, opened when a scene build (or a full probe bake, which
        // includes one) starts and closed when it finishes. The handles survive across frames; 0 means
        // "no notification currently open".
        static GUI::Notifications::ProgressId probeBuildNotif = 0;
        static bool tracking = false;
        static bool trackingBake = false; // what the open notification is tracking, to word its ending

        // A bake reports its own combined progress (scene build + tracing) and takes over from the plain
        // scene-build one, which it would otherwise duplicate.
        const bool baking = probeManager->IsBaking();

        if(baking || probeManager->IsSceneBuilding())
        {
            const float progress = std::clamp(baking ? probeManager->GetBakeProgress() : probeManager->GetSceneBuildProgress(), 0.0f, 1.0f);
            const int percent = (int)std::lround(progress * 100.0f);
            const std::string message = std::string(baking ? probeManager->GetBakePhase() : probeManager->GetSceneBuildPhase())
                + " - " + std::to_string(percent) + "%";

            if(!tracking)
            {
                probeBuildNotif = GUI::Notifications::BeginProgress("Baking GI probes", message);
                tracking = true;
            }
            else
            {
                GUI::Notifications::UpdateProgress(probeBuildNotif, progress, message);
            }

            trackingBake = trackingBake || baking;
        }
        else if(tracking)
        {
            if(trackingBake)
            {
                const bool ok = probeManager->DidLastBakeSucceed();
                GUI::Notifications::EndProgress(probeBuildNotif, ok, ok ? "GI probes baked - save the level to keep them" : "GI probe bake failed - see the log");
            }
            else
            {
                GUI::Notifications::EndProgress(probeBuildNotif, true, "GI probe scene ready");
            }

            tracking = false;
            trackingBake = false;
            probeBuildNotif = 0;
        }
    }

    bool EditorMainWindow::ShouldClose() const
    {
        return glfwWindowShouldClose(window);
    }

    int EditorMainWindow::GetFramebufferWidth() const
    {
        if(viewport)
            return viewport->GetViewportSize().x;
        return 50;
    }

    int EditorMainWindow::GetFramebufferHeight() const
    {
        if(viewport)
            return viewport->GetViewportSize().y;
        return 50;
    }

    void *EditorMainWindow::GetNativeHandle() const
    {
        return static_cast<void*>(window);
    }

    void EditorMainWindow::Destroy() const
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void EditorMainWindow::ToggleFullscreen()
    {
        const bool fullscreen = glfwGetWindowMonitor(window) != nullptr;
        if(fullscreen) {
            // Restore the window position and size.
            glfwSetWindowMonitor(window, nullptr, windowPosX, windowPosY, windowWidth, windowHeight, 0);
            // Check the window position and size (if we are on a screen smaller than the initial size).
            glfwGetWindowPos(window, &windowPosX, &windowPosY);
            glfwGetWindowSize(window, &windowWidth, &windowHeight);
        } else {
            // Backup the window current frame.
            glfwGetWindowPos(window, &windowPosX, &windowPosY);
            glfwGetWindowSize(window, &windowWidth, &windowHeight);
            // Move to fullscreen on the primary monitor.
            GLFWmonitor * monitor	= glfwGetPrimaryMonitor();
            const GLFWvidmode * mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
    }

    void EditorMainWindow::RequestExit()
    {
        glfwSetWindowShouldClose(window, true);
    }

    void EditorMainWindow::ProcessInputs() const
    {
        if(Engine::Core::GetEngine().IsInPlayMode())
            return;

        Engine::Core::Platform::IInput* input = Engine::Core::GetEngine().GetInputManager();

        if(input->IsKeyDown(Engine::Input::Key::LeftControl)){
        
            if(input->WasKeyPressed(Engine::Input::Key::S)){
                Engine::Core::GetEngine().GetLevelManager()->GetLevelAt(0)->Serialize(
                    Engine::Core::GetEngine().GetLevelManager()->GetLevelAt(0)->GetPath()
                );
                return;
            }
            else if(input->WasKeyPressed(Engine::Input::Key::W)){
                Editor::Commands::CommandStack::Get().Undo();
                return;
            }
            else if(input->WasKeyPressed(Engine::Input::Key::Y)){
                Editor::Commands::CommandStack::Get().Redo();
                return;
            }
        }

        viewport->ProcessInputs();
    }

    int EditorMainWindow::GetBytesPerPixel() const
    {
        int redBits = glfwGetWindowAttrib(window, GLFW_RED_BITS);
        int greenBits = glfwGetWindowAttrib(window, GLFW_GREEN_BITS);
        int blueBits = glfwGetWindowAttrib(window, GLFW_BLUE_BITS);
        int alphaBits = glfwGetWindowAttrib(window, GLFW_ALPHA_BITS);

        return (redBits + greenBits + blueBits + alphaBits) / 8;
    }

    Shard::Engine::Core::Platform::SystemInfos EditorMainWindow::GetSystemInfos() const
    {
        Engine::Core::Platform::SystemInfos ret{};

        ret.gpu_vendor   = Engine::Core::GetEngine().GetRenderer()->GetDeviceVendor();
        ret.gpu_renderer = Engine::Core::GetEngine().GetRenderer()->GetRendererName();
        ret.gl_version   =  Engine::Core::GetEngine().GetRenderer()->GetDriverVersion();

        // GLFW context version
        int major, minor, rev;
        glfwGetVersion(&major, &minor, &rev);
        ret.windowHostVersion = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(rev);

        // Monitor and resolution
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        ret.connectedMonitorsCount = count;

        for (int i = 0; i < count; ++i) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            ret.monitors.push_back({mode->width, mode->height, mode->refreshRate});
        }
        
        ret.windowHost = Engine::Core::Platform::GLFW;

        return ret;
    }
}

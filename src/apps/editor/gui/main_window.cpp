#include "main_window.hpp"
#include "engine/plugins/manager.hpp"
#include <stb/stb_image.h>

#include "imgui/backends/imgui_impl_vulkan.h"

#include "engine/world/engine.hpp"

#include "apps/editor/gui/panels/asset_browser/asset_browser.hpp"
#include "apps/editor/commands/command_stack.hpp"
#include "engine/world/event_system.hpp"

#include <algorithm>
#include <map>
#include <unordered_set>
#include <cmath>
#include <fstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>

#include "apps/editor/gui/IconsLucide.h"
#include "apps/editor/gui/notifications.hpp"
#include "apps/editor/gui/popups.hpp"
#include "apps/editor/gui/loading_widgets.hpp"

#include "engine/renderer/features/lighting/probe_manager.hpp"
#include "engine/renderer/features/lighting/lightmap_manager.hpp"

#include "engine/assets/resources_manager.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/assets/project/project.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/components/model_component.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/world.hpp"

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
        colors[ImGuiCol_WindowBg]               = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
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

    void EditorMainWindow::SetOutlined(const std::shared_ptr<Engine::Objects::Actor>& actor, bool outlined)
    {
        // EditorOutlineMaskPass only ever contains the selected actors' own model(s) (see the matching comment where the
        // pass is created in SwapBuffers) - changing the selection means pulling the old actors' draw commands out and
        // pushing the new ones' in, rather than drawing the whole scene and sorting it out per-fragment with an objID compare.
        if(!renderPassesInitialized || !actor)
            return;

        for(auto& comp : actor->GetComponents())
            if(auto model = std::dynamic_pointer_cast<Engine::Objects::Components::Model>(comp)){
                if(outlined)
                    model->AddToPass("EditorOutlineMaskPass");
                else
                    model->RemoveFromPass("EditorOutlineMaskPass");
            }
    }

    void EditorMainWindow::ApplySelection(std::vector<std::shared_ptr<Engine::Objects::Actor>> actors)
    {
        // No nulls, no actor twice (the last occurrence stays : it is the one that ends up primary)
        std::vector<std::shared_ptr<Engine::Objects::Actor>> unique;
        std::unordered_set<int> seen;
        for(auto it = actors.rbegin(); it != actors.rend(); ++it)
            if(*it && seen.insert((*it)->GetID().GetAsInt()).second)
                unique.push_back(*it);
        std::reverse(unique.begin(), unique.end());

        for(auto& old : selectedActors)
            if(!seen.count(old->GetID().GetAsInt()))
                SetOutlined(old, false);

        for(auto& actor : unique)
            if(!selectedIds.count(actor->GetID().GetAsInt()))
                SetOutlined(actor, true);

        selectedActors = std::move(unique);
        selectedIds = std::move(seen);

        selectedRefs.clear();
        for(auto& actor : selectedActors)
            selectedRefs.push_back(Editor::Commands::ObjectRefs::Get(actor));
    }

    void EditorMainWindow::SetSelectedActor(std::shared_ptr<Engine::Objects::Actor> newPtr)
    {
        ApplySelection({ newPtr });
    }

    void EditorMainWindow::SetSelectedActors(std::vector<std::shared_ptr<Engine::Objects::Actor>> actors)
    {
        ApplySelection(std::move(actors));
    }

    bool EditorMainWindow::IsSelected(const std::shared_ptr<Engine::Objects::Actor>& actor) const
    {
        return actor && selectedIds.count(actor->GetID().GetAsInt()) > 0;
    }

    void EditorMainWindow::AddToSelection(const std::shared_ptr<Engine::Objects::Actor>& actor)
    {
        if(!actor)
            return;

        auto actors = selectedActors;
        actors.push_back(actor);
        ApplySelection(std::move(actors));
    }

    void EditorMainWindow::RemoveFromSelection(const std::shared_ptr<Engine::Objects::Actor>& actor)
    {
        if(!IsSelected(actor))
            return;

        auto actors = selectedActors;
        actors.erase(std::remove(actors.begin(), actors.end(), actor), actors.end());
        ApplySelection(std::move(actors));
    }

    void EditorMainWindow::ToggleSelection(const std::shared_ptr<Engine::Objects::Actor>& actor)
    {
        if(IsSelected(actor))
            RemoveFromSelection(actor);
        else
            AddToSelection(actor);
    }

    namespace {

        // Every actor of the world, the roots first then their descendants, depth first
        void CollectActors(const std::shared_ptr<Engine::Objects::Actor>& actor, std::vector<std::shared_ptr<Engine::Objects::Actor>>& out)
        {
            out.push_back(actor);
            for(auto childID : actor->GetChildrenID(false))
                if(auto child = std::dynamic_pointer_cast<Engine::Objects::Actor>(actor->GetChild(childID)))
                    CollectActors(child, out);
        }

    }

    std::vector<std::shared_ptr<Engine::Objects::Actor>> EditorMainWindow::GetAllActors() const
    {
        std::vector<std::shared_ptr<Engine::Objects::Actor>> all;
        if(auto* world = Engine::Core::GetEngine().GetWorldManager()->GetWorldAt(0)){
            // By id : the same order every time
            std::map<int, std::shared_ptr<Engine::Objects::Actor>> roots;
            for(auto& [id, actor] : world->GetRootActors())
                if(actor)
                    roots[id.GetAsInt()] = actor;
            for(auto& [id, actor] : roots)
                CollectActors(actor, all);
        }
        return all;
    }

    void EditorMainWindow::SelectAll()
    {
        SetSelectedActors(GetAllActors());
    }

    void EditorMainWindow::InvertSelection()
    {
        std::vector<std::shared_ptr<Engine::Objects::Actor>> inverted;
        for(auto& actor : GetAllActors())
            if(!IsSelected(actor))
                inverted.push_back(actor);
        SetSelectedActors(std::move(inverted));
    }

    void EditorMainWindow::SelectChildren()
    {
        auto actors = selectedActors;
        for(auto& actor : selectedActors)
            for(auto childID : actor->GetChildrenID(true))
                if(auto child = std::dynamic_pointer_cast<Engine::Objects::Actor>(
                        Engine::Core::GetEngine().GetObjectIDManager()->GetObjectFromID(childID)))
                    if(!IsSelected(child))
                        actors.insert(actors.begin(), child);
        // The primary actor stays the primary one
        SetSelectedActors(std::move(actors));
    }

    void EditorMainWindow::ValidateSelection()
    {
        if(selectedActors.empty())
            return;

        auto* ids = Engine::Core::GetEngine().GetObjectIDManager();

        bool allAlive = true;
        for(auto& actor : selectedActors)
            if(ids->GetObjectFromID(actor->GetID()) != actor){
                allAlive = false;
                break;
            }
        if(allAlive)
            return;

        // The dead ones are replaced by what an undo or a redo rebuilt in their place, if anything
        std::vector<std::shared_ptr<Engine::Objects::Actor>> actors;
        for(size_t i = 0; i < selectedActors.size(); i++){
            if(ids->GetObjectFromID(selectedActors[i]->GetID()) == selectedActors[i])
                actors.push_back(selectedActors[i]);
            else if(i < selectedRefs.size() && selectedRefs[i])
                if(auto rebuilt = selectedRefs[i]->Resolve<Engine::Objects::Actor>())
                    actors.push_back(rebuilt);
        }
        ApplySelection(std::move(actors));
    }

    void EditorMainWindow::CopySelection()
    {
        actorClipboard.Copy(selectedActors);
    }

    void EditorMainWindow::CutSelection()
    {
        if(selectedActors.empty())
            return;

        auto actors = selectedActors;
        ClearSelection();
        actorClipboard.Cut(actors);
    }

    void EditorMainWindow::PasteClipboard()
    {
        auto pasted = actorClipboard.Paste();
        if(!pasted.empty())
            SetSelectedActors(std::move(pasted));
    }

    void EditorMainWindow::DuplicateSelection()
    {
        auto copies = Editor::Commands::Actions::DuplicateMany(selectedActors);
        if(!copies.empty())
            SetSelectedActors(std::move(copies));
    }

    void EditorMainWindow::DeleteSelection()
    {
        if(selectedActors.empty())
            return;

        auto actors = selectedActors;
        ClearSelection();
        Editor::Commands::Actions::DeleteMany(actors);
    }

    void EditorMainWindow::GroupSelection()
    {
        if(selectedActors.empty())
            return;

        if(auto group = Editor::Commands::Actions::GroupActors(selectedActors))
            SetSelectedActor(group);
    }

    void EditorMainWindow::RenameSelection()
    {
        if(worldTree && GetSelectedActor())
            worldTree->BeginRename(GetSelectedActor());
    }

    void EditorMainWindow::RequestPlay()
    {
        auto& engine = Engine::Core::GetEngine();
        if(engine.IsInPlayMode())
            return;

        auto startPlay = [this]{
            ClearSelection();
            Engine::Core::GetEngine().SetPlayMode(true);
        };

        Engine::Worlds::World* world = engine.GetWorldManager()->GetWorldAt(0);
        if(!world || !world->IsDirty()){
            startPlay();
            return;
        }

        // The play session starts from the saved world and the editor returns to it on stop : unsaved changes
        // have to be saved first, or they would be lost (or silently written, see EngineInstance::SetPlayMode).
        GUI::Popups::Show(GUI::Popups::PopupType::Warning, "Unsaved changes",
            "\"" + world->GetName() + "\" has unsaved changes. Save the world before starting the play mode.",
            {
                { "Save and play", [startPlay]{
                    if(Engine::Worlds::World* current = Engine::Core::GetEngine().GetWorldManager()->GetWorldAt(0))
                        current->Serialize(current->GetPath());
                    startPlay();
                } },
                { "Cancel", nullptr }
            });
    }

    void EditorMainWindow::OnWorldLoaded()
    {
        // The previous world's draw lists were cleared before the load (Renderer::ClearPassesContent) : the old actor's
        // models are not in the outline pass any more, and their components must not be touched, so SetSelectedActor
        // is not used here.
        selectedActors.clear();
        selectedIds.clear();
        selectedRefs.clear();

        Editor::Commands::CommandStack::Get().Clear();
    }

    namespace {

        bool UsesVulkan()
        {
            Rendering::RendererAPI* api = Rendering::RendererAPI::Current();
            return api && api->GetAPI() == Rendering::RendererAPI::API::Vulkan;
        }

        void CheckVulkanResult(VkResult result)
        {
            if(result != VK_SUCCESS)
                DEBUG_ERROR("The interface's Vulkan renderer failed : VkResult ", static_cast<int>(result));
        }

        /// Sets the interface's Vulkan renderer up with the handles of the engine's backend. On the render thread.
        bool InitVulkanUI()
        {
            Rendering::RendererAPI* api = Rendering::RendererAPI::Current();

            Rendering::UIBackendInfo info;
            if(!api || !api->GetUIBackendInfo(info))
            {
                DEBUG_ERROR("The Vulkan backend has no window to draw the interface on");
                return false;
            }

            const VkFormat format = static_cast<VkFormat>(info.swapchainFormat);

            // The pointer has to outlive the backend : the interface's pipeline is made now, but it is made again when the format changes
            static VkFormat colorFormat;
            colorFormat = format;

            ImGui_ImplVulkan_InitInfo init{};
            init.ApiVersion = info.apiVersion;
            init.Instance = static_cast<VkInstance>(info.instance);
            init.PhysicalDevice = static_cast<VkPhysicalDevice>(info.physicalDevice);
            init.Device = static_cast<VkDevice>(info.device);
            init.QueueFamily = info.queueFamily;
            init.Queue = static_cast<VkQueue>(info.queue);
            // One descriptor per texture the interface shows (the thumbnails of the asset browser, the viewport...), and the font atlas
            init.DescriptorPoolSize = 2048;
            init.MinImageCount = 2;
            init.ImageCount = info.imageCount;
            init.UseDynamicRendering = true;
            init.PipelineInfoMain.PipelineRenderingCreateInfo = {};
            init.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
            init.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
            init.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
            init.CheckVkResultFn = CheckVulkanResult;

            if(!ImGui_ImplVulkan_Init(&init))
            {
                DEBUG_ERROR("The interface's Vulkan renderer could not be created");
                return false;
            }

            // The textures the interface shows are named by their handle in its draw lists ; the render thread turns them into descriptors
            // when it draws (see ConvertTextureIDs)
            Rendering::UITextureHooks hooks;
            hooks.add = [](void* sampler, void* view, uint32_t layout) -> uint64_t {
                return reinterpret_cast<uint64_t>(ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(sampler), static_cast<VkImageView>(view), static_cast<VkImageLayout>(layout)));
            };
            hooks.remove = [](uint64_t id) { ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(id)); };
            api->SetUITextureHooks(hooks);
            return true;
        }

        /// The draw lists carry the handle of the engine's textures : on Vulkan the interface draws descriptor sets. The fonts are the backend's.
        void ConvertTextureIDs(ImDrawData* drawData)
        {
            Rendering::RendererAPI* api = Rendering::RendererAPI::Current();

            for(int n = 0; n < drawData->CmdListsCount; n++)
            {
                for(ImDrawCmd& command : drawData->CmdLists[n]->CmdBuffer)
                {
                    if(command.TexRef._TexData != nullptr)
                        continue;

                    const ImTextureID handle = command.TexRef._TexID;
                    if(handle != 0)
                        command.TexRef._TexID = api->GetUITextureID(static_cast<uint32_t>(handle));
                }
            }
        }

        const float kClearColor[4] = {0.1f, 0.1f, 0.1f, 1.0f};

        /// Draws the interface as the picture of the frame (Vulkan). On the render thread.
        void PresentVulkanUI(ImDrawData* drawData, uint32_t width, uint32_t height)
        {
            ConvertTextureIDs(drawData);
            // Sampled on the render thread : the acquire and the present block on the swapchain / vsync
            SHARD_PROFILE_SCOPE(Engine::Debugging::ProfileCategory::Presentation);
            Rendering::RendererAPI::Current()->PresentUI([drawData](void* commandBuffer) {
                ImGui_ImplVulkan_RenderDrawData(drawData, static_cast<VkCommandBuffer>(commandBuffer));
            }, width, height, kClearColor);
        }
    }

    void EditorMainWindow::Init(const std::string &title, const int &width, const int &height, const bool &fullscreen, const int &vsync, const uint32_t& api)
    {
        SDLWindow::Init(title, width, height, fullscreen, vsync, api);

        // The window's icon (taskbar, title bar) : the Shard logo
        if(SDL_Window* window = GetSDLWindow())
        {
            int iconWidth = 0, iconHeight = 0, iconChannels = 0;
            if(stbi_uc* pixels = stbi_load("editor_resources/icons/shard_logo.png", &iconWidth, &iconHeight, &iconChannels, 4))
            {
                // SDL copies the pixels into the icon, so they can go right after
                if(SDL_Surface* icon = SDL_CreateSurfaceFrom(iconWidth, iconHeight, SDL_PIXELFORMAT_RGBA32, pixels, iconWidth * 4))
                {
                    SDL_SetWindowIcon(window, icon);
                    SDL_DestroySurface(icon);
                }
                stbi_image_free(pixels);
            }
            else
                DEBUG_WARNING("Could not load the editor icon (editor_resources/icons/shard_logo.png)");
        }

        if(api == (uint32_t)Engine::Rendering::RendererAPI::API::OpenGL)
            gladLoadGL((GLADloadfunc)SDLWindow::GLProcLoader);
        else if(api == (uint32_t)Engine::Rendering::RendererAPI::API::Vulkan)
        {
            //gladLoadVulkan(Engine::Core::GetEngine().GetRenderer()->GetDevicePointer?,(GLADloadfunc)SDLWindow::GLProcLoader);
        }
    }

    void EditorMainWindow::OnNativeEvent(const SDL_Event& event)
    {
        if(imguiInitialized)
            ImGui_ImplSDL3_ProcessEvent(&event);
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
        if(UsesVulkan())
        {
            ImGui_ImplSDL3_InitForVulkan(GetSDLWindow());
            Rendering::OnGraphicsThread([] { InitVulkanUI(); });
        }
        else
        {
            ImGui_ImplSDL3_InitForOpenGL(GetSDLWindow(), GetGLContext());
            Rendering::OnGraphicsThread([] { ImGui_ImplOpenGL3_Init(); });
        }

        if(!LoadEditorPreferences(GetEditorPreferencesPath(), preferences, &savedPreferencesText))
            DEBUG_INFO("No editor preferences yet (" + GetEditorPreferencesPath() + ") : using the defaults");
        Editor::Commands::CommandStack::Get().SetMaxHistory(static_cast<size_t>(preferences.saving.undoHistorySize));
        ApplyAppearance();

        LoadFontFromFile("editor_resources/fonts/OpenSans-Regular.ttf", 16, false);
        LoadFontFromFile("editor_resources/fonts/lucide.ttf", 16, true);

        //Init Panels
        assetBrowser = new GUI::AssetBrowser();
        assetBrowser->NavigateTo(Engine::Core::GetEngine().GetCurrentProject()->GetProjectResourcesPath().full);
        assetBrowser->SetParentWindow(this);
        materialEditorPanel = new GUI::MaterialEditorPanel();
        GUI::AssetEditorRegistry::Instance().Register(Engine::Filesystem::Type::T_MATERIAL, materialEditorPanel);
        propertiesPanel = new GUI::PropertiesPanel();
        propertiesPanel->SetAssetBrowser(assetBrowser);
        worldTree = new GUI::WorldTree();
        worldTree->SetParentWindow(this);
        worldSettingsPanel = new GUI::WorldSettingsPanel();
        projectSettingsPanel = new GUI::ProjectSettingsPanel();
        editorPreferencesPanel = new GUI::EditorPreferencesPanel();
        editorPreferencesPanel->SetParentWindow(this);
        viewport = new GUI::ViewportWindow();
        viewport->SetParentWindow(this);
        console = new GUI::Console();
        console->SetParentWindow(this);
        profilerPanel = new GUI::ProfilerPanel();
        pluginsPanel = new GUI::PluginsPanel();
        menuBar = new GUI::MenuBar();
        menuBar->SetParentWindow(this);

        GUI::EditorResources::Instance().Init();

        Engine::Core::GetEngine().GetEventDispatcher()->subscribeGlobal<Engine::Events::WorldStructureChangedEvent>(
            [this](const Engine::Events::WorldStructureChangedEvent& e){
                if(e.changeType == Engine::Events::LOADED)
                    OnWorldLoaded();
            });

        imguiInitialized = true;
    }

    void EditorMainWindow::DrawLoadingOverlay(float progress)
    {
        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x + mainViewport->WorkSize.x * 0.5f,
                                        mainViewport->WorkPos.y + mainViewport->WorkSize.y * 0.5f),
                                        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        // Fully opaque - this overlay stands in for the whole editor while a world loads, so letting the
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

        if(ImGui::Begin("##LoadingWorldOverlay", nullptr, flags))
        {
            // Matches the editor's own blue-teal accent (see ImGuiCol_SliderGrabActive/ButtonHovered in
            // SetupImGuiStyle) so this overlay doesn't look like it belongs to a different app.
            const ImU32 accent = IM_COL32(84, 125, 153, 255);
            constexpr float barWidth = 280.0f;
            constexpr float spinnerRadius = 14.0f;

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            ImVec2 spinnerTopLeft = ImGui::GetCursorScreenPos();
            ImVec2 spinnerCentre(spinnerTopLeft.x + barWidth * 0.5f, spinnerTopLeft.y + spinnerRadius);
            GUI::LoadingWidgets::SpinnerFadePulsar(drawList, spinnerCentre, spinnerRadius, accent, 1.8f, 2);
            ImGui::Dummy(ImVec2(barWidth, spinnerRadius * 2.0f + 14.0f));

            const char* label = "Loading World...";
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

        Rendering::RendererAPI* api = Rendering::RendererAPI::Current();
        api->SetClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        api->Clear(Rendering::ClearBit::Color | Rendering::ClearBit::Depth);

        if(UsesVulkan())
            ImGui_ImplVulkan_NewFrame();
        else
            ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        DrawLoadingOverlay(progress);

        GUI::Notifications::RenderFrame();
        GUI::Popups::Draw();

        ImGui::Render();

        if(UsesVulkan())
        {
            ImDrawData* drawData = ImGui::GetDrawData();
            const uint32_t width = static_cast<uint32_t>(SDLWindow::GetFramebufferWidth());
            const uint32_t height = static_cast<uint32_t>(SDLWindow::GetFramebufferHeight());
            Rendering::OnGraphicsThread([drawData, width, height] { PresentVulkanUI(drawData, width, height); });
            return;
        }

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDLWindow::SwapBuffers();
    }

    void EditorMainWindow::SwapBuffers()
    {
        EnsureImGuiInitialized();

        Rendering::Renderer* renderer = Engine::Core::GetEngine().GetRenderer();

        // Wait for viewport size to be initialized
        if(!renderPassesInitialized && viewport->GetViewportSize() != glm::vec2(50,50)
            && viewport->GetViewportSize().x > 0 && viewport->GetViewportSize().y > 0){

            // The colliders, the volumes and the probes : drawn by the editor, the engine knows none of them
            debugDraw.Init();

            /// Outline mask pass

            Rendering::FramebufferSpecifications fbOutlineMaskSpecs;
            fbOutlineMaskSpecs.hasColor = true;
            fbOutlineMaskSpecs.hasDepth = true;
            fbOutlineMaskSpecs.colorSpecs = {};
            fbOutlineMaskSpecs.width = viewport->GetViewportSize().x;
            fbOutlineMaskSpecs.height = viewport->GetViewportSize().y;

            std::shared_ptr<Rendering::Framebuffer> fbOutlineMask = Rendering::Framebuffer::Create(fbOutlineMaskSpecs);

            std::shared_ptr<Rendering::Shader> outlineMaskShader = Engine::Core::GetEngine().GetResourcesManager()->Get<Engine::Rendering::Shader>(Engine::Core::Resources::AssetKind::Shader, "shaders/editor/outline_mask");
            
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
            for(auto& actor : selectedActors)
                for(auto& comp : actor->GetComponents())
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
            jfaInitPipelineSpecs.shader = Engine::Core::GetEngine().GetResourcesManager()->Get<Engine::Rendering::Shader>(Engine::Core::Resources::AssetKind::Shader, "shaders/editor/jfa_init");
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
            jfaStepPipelineSpecs.shader = Engine::Core::GetEngine().GetResourcesManager()->Get<Engine::Rendering::Shader>(Engine::Core::Resources::AssetKind::Shader, "shaders/editor/jfa_step");
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

            std::shared_ptr<Rendering::Shader> outlineShader = Engine::Core::GetEngine().GetResourcesManager()->Get<Engine::Rendering::Shader>(Engine::Core::Resources::AssetKind::Shader, "shaders/editor/outline");

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

            // After everything the editor draws over the scene, so the outline lands on top of it
            renderer->AddRenderPass(outlinePass, "EditorOutlinePass", {prevPassName,
                Debug::EditorDebugDraw::PassOf(Debug::DebugLayer::Volumes),
                Debug::EditorDebugDraw::PassOf(Debug::DebugLayer::Probes),
                Debug::EditorDebugDraw::PassOf(Debug::DebugLayer::Physics)});

            Rendering::DrawCommand cmd{};
            cmd.fullscreenTri = true;
            cmd.bindCameraState = false;
            cmd.material = outlineMaterial;

            renderer->AddOrUpdateCommands({cmd}, {"EditorOutlinePass"}, false);
            outlinePipelineFullscreenCommands.push_back({"EditorOutlinePass", outlineMaterial});

            /// Infinite grid - one fullscreen pass per plane, ray-plane intersected in the fragment
            /// shader and depth-tested against the scene (it writes gl_FragDepth, not depth itself).

            std::shared_ptr<Rendering::Shader> gridShader = Engine::Core::GetEngine().GetResourcesManager()->Get<Engine::Rendering::Shader>(Engine::Core::Resources::AssetKind::Shader, "shaders/editor/grid");

            Rendering::PipelineSpecifications gridPipelineSpecs;
            gridPipelineSpecs.depthTest = true;
            gridPipelineSpecs.depthWrite = false;
            gridPipelineSpecs.blending = true;
            gridPipelineSpecs.cullMode = Rendering::CullMode::None;
            gridPipelineSpecs.shader = gridShader;
            gridPipelineSpecs.debugName = "EditorGridPipeline";
            gridPipelineSpecs.vertexLayout = {};
            std::shared_ptr<Rendering::Pipeline> gridPipeline = renderer->GetOrAddPipeline(gridPipelineSpecs);
            // Translucent : the material constructor overwrites the pipeline's blend state from its
            // opacity (Opaque would silently turn blending off).
            std::shared_ptr<Rendering::Material> gridMaterial = Rendering::Material::Create(gridShader, gridPipeline, false, Rendering::Opacity::Translucent);

            const char* gridPassNames[3] = {"EditorGridPassX", "EditorGridPassY", "EditorGridPassZ"};
            for (int axis = 0; axis < 3; axis++)
            {
                std::shared_ptr<Rendering::RenderPass> gridPass = std::make_shared<Rendering::RenderPass>();
                gridPass->clearColor = false;
                gridPass->clearDepth = false;
                gridPass->customPipeline = gridPipeline;
                gridPass->target = renderer->GetViewportFramebuffer();
                gridPass->overridePipeline = true;
                gridPass->customUniforms["axis"] = axis;
                gridPass->customUniforms["cellSize"] = 1.0f;

                renderer->AddRenderPass(gridPass, gridPassNames[axis], {"ForwardPass"});

                Rendering::DrawCommand gridCmd{};
                gridCmd.fullscreenTri = true;
                gridCmd.bindCameraState = true;
                gridCmd.material = gridMaterial;
                renderer->AddOrUpdateCommands({gridCmd}, {gridPassNames[axis]}, false);
                gridCommands.push_back({gridPassNames[axis], gridMaterial});
            }

            renderPassesInitialized = true;
        }

        if(renderPassesInitialized)
        {
            Rendering::DebugViewState debugView;
            debugView.mode = settings.viewMode;
            debugView.showLighting = settings.showLighting;
            debugView.showShadows = settings.showShadows;
            renderer->SetDebugView(debugView);
            renderer->SetShowEditorBillboards(settings.showBillboards);

            debugDraw.SetLayerVisible(Debug::DebugLayer::Volumes, settings.showGizmos);
            debugDraw.SetLayerVisible(Debug::DebugLayer::Probes, settings.showDDGIGizmos);
            debugDraw.SetLayerVisible(Debug::DebugLayer::Physics, settings.showPhysicsShapes);
            debugDraw.Update();

            // Re-submit the selected actor's model(s) every frame rather than only on selection change -
            // AddToPass captures the transform's current matrix at call time (see
            // Mesh::CreateDrawCommands), so without this the mask would keep drawing the object at
            // whatever position/rotation/scale it had at the moment it got selected, going stale the
            // instant it's moved by a gizmo drag, a script, or physics. AddOrUpdateCommands treats this
            // as a cheap update-in-place (matching commandID) rather than a fresh registration.
            for(auto& actor : selectedActors)
                for(auto& comp : actor->GetComponents())
                    if(auto model = std::dynamic_pointer_cast<Engine::Objects::Components::Model>(comp))
                        model->AddToPass("EditorOutlineMaskPass");

            // Grid squares follow the location snap increment (1m when snapping is off).
            const float gridCell = (settings.snapLocation && settings.locationSnap > 0.0f) ? settings.locationSnap : 1.0f;
            const bool gridPlaneEnabled[3] = { settings.showGridX, settings.showGridY, settings.showGridZ };
            const bool showOutlines = settings.showOutlines;
            const bool showGrid = settings.showGrid;

            // What the editor draws over the scene is the renderer's own state : it is set from the render thread, between two
            // frames, from copies of the editor settings (this thread goes on without waiting). The passes' flags and
            // uniforms apply to the frame after the one being drawn.
            Rendering::RenderThread::Get().Post([renderer, showOutlines, showGrid, gridPlaneEnabled, gridCell,
                                                 gridCommands = gridCommands, fullscreenCommands = outlinePipelineFullscreenCommands]()
            {
                renderer->GetRenderPass("EditorOutlineMaskPass")->enabled = showOutlines;
                renderer->GetRenderPass("EditorOutlinePass")->enabled = showOutlines;

                const char* gridPassNames[3] = {"EditorGridPassX", "EditorGridPassY", "EditorGridPassZ"};
                for (int axis = 0; axis < 3; axis++)
                {
                    auto gridPass = renderer->GetRenderPass(gridPassNames[axis]);
                    gridPass->enabled = showGrid && gridPlaneEnabled[axis];
                    gridPass->customUniforms["cellSize"] = gridCell;
                }

                // Grid passes also lose their command on a world reload.
                for(auto& [passName, material] : gridCommands)
                {
                    Rendering::DrawCommand gridCmd{};
                    gridCmd.fullscreenTri = true;
                    gridCmd.bindCameraState = true;
                    gridCmd.material = material;
                    renderer->AddOrUpdateCommands({gridCmd}, {passName}, false);
                }

                // Same reasoning as above, but for the JFA/composite passes' single permanent fullscreen
                // command each (see where outlinePipelineFullscreenCommands is populated in the one-time
                // setup above) - a world reload clears every pass's draw list (ClearPassesContent), and
                // that setup never runs again, so without this the outline silhouette mask would keep
                // updating correctly on selection while nothing ever turns it into a visible outline again.
                for(auto& [passName, material] : fullscreenCommands)
                {
                    Rendering::DrawCommand fsCmd{};
                    fsCmd.fullscreenTri = true;
                    fsCmd.bindCameraState = false;
                    fsCmd.material = material;
                    renderer->AddOrUpdateCommands({fsCmd}, {passName}, false);
                }
            });
        }

        // Between two frames : a new theme or scale must not change the style halfway through one
        UpdatePreferences();

        // The graphics backend of the UI is set up (and used) by the render thread : only the platform half is here
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();


        ImGuizmo::BeginFrame();

        // An actor can be destroyed from anywhere (a panel, an undo, a parent being deleted) : the panels below
        // must not get a dead one. Again after the menu bar, whose items edit the world.
        ValidateSelection();
        menuBar->Draw();
        ValidateSelection();

        ImGui::DockSpaceOverViewport();

        if(panelVisibility.assetBrowser)
            assetBrowser->Draw();
        if(panelVisibility.viewport)
            viewport->Draw();
        if(panelVisibility.properties)
            propertiesPanel->Draw(GetSelectedActor(), static_cast<int>(selectedActors.size()));
        if(panelVisibility.worldTree)
            worldTree->Draw();
        if(panelVisibility.worldSettings)
            worldSettingsPanel->Draw();
        if(panelVisibility.projectSettings)
            projectSettingsPanel->Draw(&panelVisibility.projectSettings);
        if(panelVisibility.editorPreferences)
            editorPreferencesPanel->Draw(&panelVisibility.editorPreferences);
        if(panelVisibility.console)
            console->Draw();
        if(panelVisibility.profiler)
            profilerPanel->Draw();
        if(panelVisibility.plugins)
            pluginsPanel->Draw(&panelVisibility.plugins);
        // The windows plugins added
        for(const auto& panel : Engine::Plugins::PluginManager::Get().Extensions().Get<Engine::Plugins::EditorPanelItem>("editor.panel")){
            if(panel->visible && panel->draw)
                panel->draw(&panel->visible);
        }

        materialEditorPanel->Draw();

        if(Engine::Core::GetEngine().GetWorldManager()->IsAsyncLoadInProgress())
            DrawLoadingOverlay(Engine::Core::GetEngine().GetWorldManager()->GetAsyncLoadProgress());

        UpdateProbeBuildNotification();
        UpdateLightmapBuildNotification();
        UpdateAutoSave();
        UpdateCloseRequest();
        GUI::Notifications::RenderFrame();
        GUI::Popups::Draw();

        ImGui::Render();

        // The UI is drawn by the render thread, after the scene it shows. This thread waits for it (the draw data and the
        // textures it points at are ImGui's own, which the next frame would change), then goes on with the next simulation
        // while the render thread swaps.
        ImDrawData* drawData = ImGui::GetDrawData();

        if(UsesVulkan())
        {
            // The window's size, not the viewport's (see GetFramebufferWidth) : the swapchain is as big as the window
            const uint32_t width = static_cast<uint32_t>(SDLWindow::GetFramebufferWidth());
            const uint32_t height = static_cast<uint32_t>(SDLWindow::GetFramebufferHeight());
            // This thread is blocked until the render thread has drawn the UI and presented : that is waiting, not work
            SHARD_PROFILE_SCOPE(Engine::Debugging::ProfileCategory::Waiting);
            Rendering::OnGraphicsThread([drawData, width, height] {
                ImGui_ImplVulkan_NewFrame();
                PresentVulkanUI(drawData, width, height);
            });
            return;
        }

        SHARD_PROFILE_SCOPE(Engine::Debugging::ProfileCategory::Waiting);
        Rendering::OnGraphicsThread([drawData] {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplOpenGL3_RenderDrawData(drawData);
        });
        Rendering::RenderThread::Get().Post([this] {
            // Sampled on the render thread : it blocks on the vsync, and belongs to the frame whose picture it presents
            SHARD_PROFILE_SCOPE(Engine::Debugging::ProfileCategory::Presentation);
            SDLWindow::SwapBuffers();
        });
    }

    void EditorMainWindow::UpdateLightmapBuildNotification()
    {
        Rendering::Renderer* renderer = Engine::Core::GetEngine().GetRenderer();
        if(!renderer)
            return;

        auto lightmaps = renderer->GetLightmapManager();
        if(!lightmaps)
            return;

        // One long-lived progress notification for the whole bake, closed when it ends
        static GUI::Notifications::ProgressId lightmapNotif = 0;
        static bool tracking = false;

        if(lightmaps->IsBaking())
        {
            const float progress = std::clamp(lightmaps->GetBakeProgress(), 0.0f, 1.0f);
            const std::string message = std::string(lightmaps->GetBakePhase()) + " - " + std::to_string((int)std::lround(progress * 100.0f)) + "%";

            if(!tracking)
            {
                lightmapNotif = GUI::Notifications::BeginProgress("Baking lightmaps", message);
                tracking = true;
            }
            else
            {
                GUI::Notifications::UpdateProgress(lightmapNotif, progress, message);
            }
        }
        else if(tracking)
        {
            const bool ok = lightmaps->DidLastBakeSucceed();
            GUI::Notifications::EndProgress(lightmapNotif, ok, ok ? "Lightmaps baked - save the world to keep them" : "Lightmap bake failed or cancelled - see the log");
            tracking = false;
            lightmapNotif = 0;
        }
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
                GUI::Notifications::EndProgress(probeBuildNotif, ok, ok ? "GI probes baked - save the world to keep them" : "GI probe bake failed - see the log");
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

    void EditorMainWindow::ApplyAppearance()
    {
        ImGuiStyle& style = ImGui::GetStyle();

        // From a clean style every time : the sizes are scaled from their unscaled values, not from the last scale's
        const float fontSizeBase = style.FontSizeBase;
        const float fontScaleDpi = style.FontScaleDpi;
        style = ImGuiStyle();
        style.FontSizeBase = fontSizeBase;
        style.FontScaleDpi = fontScaleDpi;

        // Shard's sizes and roundings whatever the theme ; the other themes only bring their colours
        SetupImGuiStyle();
        switch(preferences.appearance.theme)
        {
            case EditorTheme::Shard:   break;
            case EditorTheme::Dark:    ImGui::StyleColorsDark(&style); break;
            case EditorTheme::Light:   ImGui::StyleColorsLight(&style); break;
            case EditorTheme::Classic: ImGui::StyleColorsClassic(&style); break;
        }

        const PreferenceRange scaleRange = PREFERENCE_RANGE(AppearancePreferences, uiScale);
        const float scale = std::clamp(preferences.appearance.uiScale, scaleRange.min, scaleRange.max);
        style.ScaleAllSizes(scale);
        style.FontScaleMain = scale;

        appliedAppearance = preferences.appearance;
    }

    void EditorMainWindow::UpdatePreferences()
    {
        if(preferences.appearance.theme != appliedAppearance.theme || preferences.appearance.uiScale != appliedAppearance.uiScale)
            ApplyAppearance();

        Editor::Commands::CommandStack::Get().SetMaxHistory(static_cast<size_t>(std::max(1, preferences.saving.undoHistorySize)));

        // Written once a second at most, and only when they changed : a slider being dragged does not hit the disk every frame
        const double now = ImGui::GetTime();
        if(now < nextPreferencesCheck || Engine::Core::GetEngine().GetSettings().readOnly)
            return;
        nextPreferencesCheck = now + 1.0;

        std::string text = SerializeEditorPreferences(preferences);
        if(text == savedPreferencesText)
            return;

        // Kept even when the write fails : the error is logged once, not every second
        SaveEditorPreferences(GetEditorPreferencesPath(), preferences);
        savedPreferencesText = std::move(text);
    }

    void EditorMainWindow::UpdateAutoSave()
    {
        const double now = ImGui::GetTime();
        if(!preferences.saving.autoSaveWorld)
        {
            // The interval counts from when it is turned on
            lastAutoSaveTime = now;
            return;
        }

        auto& engine = Engine::Core::GetEngine();
        // Not while playing (the world is the play session's), loading, or in the middle of a gizmo drag
        if(engine.IsInPlayMode() || engine.GetWorldManager()->IsAsyncLoadInProgress() || Editor::Commands::CommandStack::Get().IsActive())
            return;

        const double interval = std::max(0.5f, preferences.saving.autoSaveIntervalMinutes) * 60.0;
        if(now - lastAutoSaveTime < interval)
            return;
        lastAutoSaveTime = now;

        Engine::Worlds::World* world = engine.GetWorldManager()->GetWorldAt(0);
        if(!world || !world->IsDirty())
            return;

        world->Serialize(world->GetPath());
        GUI::Notifications::Info("Auto-saved \"%s\"", world->GetName().c_str());
    }

    bool EditorMainWindow::HasUnsavedProjectSettings() const
    {
        return projectSettingsPanel && !Engine::Core::GetEngine().GetSettings().readOnly && projectSettingsPanel->IsDirty();
    }

    bool EditorMainWindow::ShouldClose() const
    {
        return SDLWindow::ShouldClose() && (closeConfirmed || !HasUnsavedProjectSettings());
    }

    void EditorMainWindow::UpdateCloseRequest()
    {
        if(!SDLWindow::ShouldClose() || closeConfirmed || !HasUnsavedProjectSettings())
            return;

        // The close waits for the answer : Cancel (or Escape) keeps the editor open
        CancelClose();
        GUI::Popups::ConfirmUnsavedChanges("Project Settings",
            [this]{
                if(!projectSettingsPanel->Save())
                    return;
                closeConfirmed = true;
                RequestClose();
            },
            [this]{
                closeConfirmed = true;
                RequestClose();
            });
    }

    void EditorMainWindow::Destroy() const
    {
        // What changed in the last second (the periodic write did not see it yet)
        if(imguiInitialized && !Engine::Core::GetEngine().GetSettings().readOnly
            && SerializeEditorPreferences(preferences) != savedPreferencesText)
            SaveEditorPreferences(GetEditorPreferencesPath(), preferences);

        if(UsesVulkan())
            Rendering::OnGraphicsThread([] {
                // What the interface recorded may still be running
                Rendering::UIBackendInfo info;
                Rendering::RendererAPI::Current()->GetUIBackendInfo(info);
                if(info.device)
                    vkDeviceWaitIdle(static_cast<VkDevice>(info.device));
                ImGui_ImplVulkan_Shutdown();
            });
        else
            ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        SDLWindow::Destroy();
    }

    void EditorMainWindow::RequestExit()
    {
        RequestClose();
    }

    void EditorMainWindow::ProcessInputs() const
    {
        Engine::Core::Platform::IInput* input = Engine::Core::GetEngine().GetInputManager();

        if(Engine::Core::GetEngine().IsInPlayMode())
        {
            // The one editor shortcut that works while playing : the game owns the other keys. The viewport re-asserts the
            // editor camera on the next frame (see ViewportWindow::Draw)
            if(input->WasKeyPressed(Engine::Input::Key::F1) && !ImGui::GetIO().WantTextInput)
                Engine::Core::GetEngine().SetPlayMode(false);
            return;
        }

        if(input->IsKeyDown(Engine::Input::Key::LeftControl)){
        
            if(input->WasKeyPressed(Engine::Input::Key::S)){
                if(auto* world = Engine::Core::GetEngine().GetWorldManager()->GetWorldAt(0))
                    world->Serialize(world->GetPath());
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
        else if(!ImGui::GetIO().WantTextInput){
            if(input->WasKeyPressed(Engine::Input::Key::F))
            {
                if(input->IsKeyDown(Engine::Input::Key::LeftAlt))
                {
                    viewport->SnapSelectedToFloor();
                }
                else
                {
                    viewport->FocusSelected();
                }
            }
        }

        viewport->ProcessInputs();
    }

    Shard::Engine::Core::Platform::SystemInfos EditorMainWindow::GetSystemInfos() const
    {
        Engine::Core::Platform::SystemInfos ret = SDLWindow::GetSystemInfos();

        ret.gpu_vendor   = Engine::Core::GetEngine().GetRenderer()->GetDeviceVendor();
        ret.gpu_renderer = Engine::Core::GetEngine().GetRenderer()->GetRendererName();
        ret.driver_version = Engine::Core::GetEngine().GetRenderer()->GetDriverVersion();

        return ret;
    }
}

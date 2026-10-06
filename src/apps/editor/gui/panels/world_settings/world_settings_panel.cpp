#include "world_settings_panel.hpp"

#include "engine/world/engine.hpp"
#include "engine/assets/vfs/filesystem.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/renderer/components/render_world_data.hpp"

#include "apps/editor/gui/dragdrop/asset_drag_drop.hpp"
#include "apps/editor/gui/IconsLucide.h"

#include "imgui/imgui.h"

#include <cstring>

using namespace Shard::Engine;
using Shard::Engine::Core::GetEngine;

namespace Shard::Editor::GUI{

    void WorldSettingsPanel::Draw()
    {
        if (!ImGui::Begin("World Settings"))
        {
            ImGui::End();
            return;
        }

        Worlds::World* world = GetEngine().GetWorldManager()->GetWorldAt(0);
        if (!world)
        {
            ImGui::TextDisabled("No world loaded");
            ImGui::End();
            return;
        }

        DrawGeneralCategory();
        DrawRenderingCategory();

        ImGui::End();
    }

    void WorldSettingsPanel::DrawGeneralCategory()
    {
        Worlds::World* world = GetEngine().GetWorldManager()->GetWorldAt(0);

        if (!ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::BeginDisabled();

        char nameBuffer[256];
        strncpy(nameBuffer, world->GetName().c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer));

        char pathBuffer[512];
        strncpy(pathBuffer, world->GetPath().full.c_str(), sizeof(pathBuffer) - 1);
        pathBuffer[sizeof(pathBuffer) - 1] = '\0';
        ImGui::InputText("Path", pathBuffer, sizeof(pathBuffer));

        ImGui::EndDisabled();
    }

    void WorldSettingsPanel::ApplySkybox(Worlds::World* world, const std::string& pathInProject)
    {
        m_SkyboxError.clear();

        if (pathInProject.empty())
        {
            world->Ext<Rendering::RenderWorldData>().ClearSkybox();
            world->SetDirty(true);
            return;
        }

        auto* assets = Core::GetEngine().GetAssetIDManager();
        std::shared_ptr<Filesystem::AssetInfos> info = assets->GetAssetFromID(assets->GetIDFromNameInProject(pathInProject));

        if (!info || info->baseInfos.nameInProject != pathInProject)
        {
            m_SkyboxError = "Unknown asset : " + pathInProject;
            return;
        }

        if (info->baseInfos.type != Filesystem::Type::T_IMAGE)
        {
            m_SkyboxError = pathInProject + " isn't an image.";
            return;
        }

        if (pathInProject == world->Ext<Rendering::RenderWorldData>().GetSkyboxPath())
            return;

        if (!world->Ext<Rendering::RenderWorldData>().SetSkybox(pathInProject))
        {
            m_SkyboxError = "Couldn't use " + pathInProject + " as a skybox : it must be an equirectangular RGB image (see the console).";
            return;
        }

        world->SetDirty(true);
    }

    void WorldSettingsPanel::DrawSkyboxSection(Worlds::World* world)
    {
        if (!ImGui::TreeNodeEx("Skybox", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
            return;

        // A different world is now shown - drop the previous one's half-typed text and error.
        if (world != m_LastWorld)
        {
            m_LastWorld = world;
            m_SkyboxInputActive = false;
            m_SkyboxError.clear();
        }

        if (!m_SkyboxInputActive)
            m_SkyboxInput = world->Ext<Rendering::RenderWorldData>().GetSkyboxPath();

        char buffer[256];
        strncpy(buffer, m_SkyboxInput.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        const float clearWidth = ImGui::GetFrameHeight();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Skybox file");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(-(clearWidth + ImGui::GetStyle().ItemSpacing.x));
        const bool submitted = ImGui::InputTextWithHint("##SkyboxFile", "None - drop an image here", buffer, sizeof(buffer),
                                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        m_SkyboxInputActive = ImGui::IsItemActive();
        if (m_SkyboxInputActive)
            m_SkyboxInput = buffer;

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Equirectangular image used as the world's sky and image-based lighting.\nDrag one from the asset browser, or type its path and press Enter.");

        if (submitted)
            ApplySkybox(world, buffer);

        // Same drag-and-drop the Properties panel's asset fields accept.
        if (ImGui::BeginDragDropTarget())
        {
            std::vector<std::string> dropped = DragDrop::AcceptAssetDragDropPayload();
            if (!dropped.empty())
                ApplySkybox(world, dropped[0]);
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(world->Ext<Rendering::RenderWorldData>().GetSkyboxPath().empty());
        if (ImGui::Button((std::string(ICON_LC_X) + "##ClearSkybox").c_str(), ImVec2(clearWidth, clearWidth)))
            ApplySkybox(world, "");
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Remove the skybox");

        if (!m_SkyboxError.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.40f, 1.0f));
            ImGui::TextWrapped("%s", m_SkyboxError.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::TreePop();
    }

    void WorldSettingsPanel::DrawRenderingCategory()
    {
        Worlds::World* world = GetEngine().GetWorldManager()->GetWorldAt(0);

        if (!ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::Indent();

        DrawSkyboxSection(world);

        // Ambient - see the comment on World::ambientIntensity/lit.frag's SampleSSAO for the exact
        // semantics : an additive light floor always present, even with zero real lights/DDGI/IBL.
        if (ImGui::TreeNodeEx("Ambient", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
        {
            if (ImGui::DragFloat("Intensity", &world->ambientIntensity, 0.01f, 0.0f, 10.0f))
                world->SetDirty(true);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Screen-Space Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
        {
            if (ImGui::Checkbox("Enabled", &world->ssaoEnabled))
                world->SetDirty(true);

            if (!world->ssaoEnabled)
                ImGui::BeginDisabled();

            bool changed = false;
            changed |= ImGui::DragFloat("Radius", &world->ssaoRadius, 0.01f, 0.01f, 5.0f);
            changed |= ImGui::DragFloat("Bias", &world->ssaoBias, 0.001f, 0.0f, 0.5f);
            changed |= ImGui::DragFloat("Power", &world->ssaoPower, 0.05f, 0.5f, 6.0f);
            changed |= ImGui::DragFloat("Intensity", &world->ssaoIntensity, 0.01f, 0.0f, 3.0f);

            if (changed)
                world->SetDirty(true);

            if (!world->ssaoEnabled)
                ImGui::EndDisabled();

            ImGui::TreePop();
        }

        ImGui::Unindent();
    }
}

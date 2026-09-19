#include "level_settings_panel.hpp"

#include "engine/core/engine.hpp"
#include "engine/filesystem/filesystem.hpp"
#include "engine/levels/level.hpp"
#include "engine/levels/level_manager.hpp"

#include "editor/gui/dragdrop/asset_drag_drop.hpp"
#include "editor/gui/IconsLucide.h"

#include "imgui/imgui.h"

#include <cstring>

using namespace Shard::Engine;
using Shard::Engine::Core::GetEngine;

namespace Shard::Editor::GUI{

    void LevelSettingsPanel::Draw()
    {
        if (!ImGui::Begin("Level Settings"))
        {
            ImGui::End();
            return;
        }

        Levels::Level* level = GetEngine().GetLevelManager()->GetLevelAt(0);
        if (!level)
        {
            ImGui::TextDisabled("No level loaded");
            ImGui::End();
            return;
        }

        DrawGeneralCategory();
        DrawRenderingCategory();

        ImGui::End();
    }

    void LevelSettingsPanel::DrawGeneralCategory()
    {
        Levels::Level* level = GetEngine().GetLevelManager()->GetLevelAt(0);

        if (!ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::BeginDisabled();

        char nameBuffer[256];
        strncpy(nameBuffer, level->GetName().c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer));

        char pathBuffer[512];
        strncpy(pathBuffer, level->GetPath().full.c_str(), sizeof(pathBuffer) - 1);
        pathBuffer[sizeof(pathBuffer) - 1] = '\0';
        ImGui::InputText("Path", pathBuffer, sizeof(pathBuffer));

        ImGui::EndDisabled();
    }

    void LevelSettingsPanel::ApplySkybox(Levels::Level* level, const std::string& pathInProject)
    {
        m_SkyboxError.clear();

        if (pathInProject.empty())
        {
            level->ClearSkybox();
            level->SetDirty(true);
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

        if (pathInProject == level->GetSkyboxPath())
            return;

        if (!level->SetSkybox(pathInProject))
        {
            m_SkyboxError = "Couldn't use " + pathInProject + " as a skybox : it must be an equirectangular RGB image (see the console).";
            return;
        }

        level->SetDirty(true);
    }

    void LevelSettingsPanel::DrawSkyboxSection(Levels::Level* level)
    {
        if (!ImGui::TreeNodeEx("Skybox", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
            return;

        // A different level is now shown - drop the previous one's half-typed text and error.
        if (level != m_LastLevel)
        {
            m_LastLevel = level;
            m_SkyboxInputActive = false;
            m_SkyboxError.clear();
        }

        if (!m_SkyboxInputActive)
            m_SkyboxInput = level->GetSkyboxPath();

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
            ImGui::SetTooltip("Equirectangular image used as the level's sky and image-based lighting.\nDrag one from the asset browser, or type its path and press Enter.");

        if (submitted)
            ApplySkybox(level, buffer);

        // Same drag-and-drop the Properties panel's asset fields accept.
        if (ImGui::BeginDragDropTarget())
        {
            std::vector<std::string> dropped = DragDrop::AcceptAssetDragDropPayload();
            if (!dropped.empty())
                ApplySkybox(level, dropped[0]);
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(level->GetSkyboxPath().empty());
        if (ImGui::Button((std::string(ICON_LC_X) + "##ClearSkybox").c_str(), ImVec2(clearWidth, clearWidth)))
            ApplySkybox(level, "");
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

    void LevelSettingsPanel::DrawRenderingCategory()
    {
        Levels::Level* level = GetEngine().GetLevelManager()->GetLevelAt(0);

        if (!ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::Indent();

        DrawSkyboxSection(level);

        // Ambient - see the comment on Level::ambientIntensity/lit.frag's SampleSSAO for the exact
        // semantics : an additive light floor always present, even with zero real lights/DDGI/IBL.
        if (ImGui::TreeNodeEx("Ambient", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
        {
            if (ImGui::DragFloat("Intensity", &level->ambientIntensity, 0.01f, 0.0f, 10.0f))
                level->SetDirty(true);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Screen-Space Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
        {
            if (ImGui::Checkbox("Enabled", &level->ssaoEnabled))
                level->SetDirty(true);

            if (!level->ssaoEnabled)
                ImGui::BeginDisabled();

            bool changed = false;
            changed |= ImGui::DragFloat("Radius", &level->ssaoRadius, 0.01f, 0.01f, 5.0f);
            changed |= ImGui::DragFloat("Bias", &level->ssaoBias, 0.001f, 0.0f, 0.5f);
            changed |= ImGui::DragFloat("Power", &level->ssaoPower, 0.05f, 0.5f, 6.0f);
            changed |= ImGui::DragFloat("Intensity", &level->ssaoIntensity, 0.01f, 0.0f, 3.0f);

            if (changed)
                level->SetDirty(true);

            if (!level->ssaoEnabled)
                ImGui::EndDisabled();

            ImGui::TreePop();
        }

        ImGui::Unindent();
    }
}

#include "project_settings_panel.hpp"

#include "engine/core/engine.hpp"
#include "engine/levels/level.hpp"
#include "engine/levels/level_manager.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/projects/project.hpp"
#include "engine/serialization/project/project_serializer.hpp"
#include "engine/time/time_manager.hpp"

#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <string>

using namespace Shard::Engine;
using Shard::Engine::Core::GetEngine;

namespace Shard::Editor::GUI{

    bool ProjectSettingsPanel::Matches(const char* label) const
    {
        if (m_Search[0] == '\0')
            return true;

        std::string haystack = label;
        std::string needle = m_Search;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c){ return std::tolower(c); });
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c){ return std::tolower(c); });
        return haystack.find(needle) != std::string::npos;
    }

    bool ProjectSettingsPanel::BeginRow(const char* label, const char* tooltip)
    {
        if (!Matches(label))
            return false;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (tooltip && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);

        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.35f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        return true;
    }

    void ProjectSettingsPanel::Save()
    {
        auto project = GetEngine().GetCurrentProject();
        if (!project)
            return;

        Serialization::SerializeProject(project.get(), Filesystem::Path(GetEngine().GetSettings().project));
        m_Dirty = false;
    }

    void ProjectSettingsPanel::Draw(bool* open)
    {
        ImGui::SetNextWindowSize(ImVec2(760, 460), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Project Settings", open))
        {
            ImGui::End();
            return;
        }

        auto project = GetEngine().GetCurrentProject();
        if (!project)
        {
            ImGui::TextDisabled("No project loaded");
            ImGui::End();
            return;
        }

        // Top bar : search + save.
        const float saveWidth = 110.0f;
        ImGui::SetNextItemWidth(-(saveWidth + ImGui::GetStyle().ItemSpacing.x));
        ImGui::InputTextWithHint("##ProjectSettingsSearch", "Search settings", m_Search, sizeof(m_Search));
        ImGui::SameLine();
        if (ImGui::Button(m_Dirty ? "Save Project *" : "Save Project", ImVec2(saveWidth, 0)))
            Save();

        ImGui::Separator();

        // Left : categories.
        ImGui::BeginChild("##Categories", ImVec2(160, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        struct Entry { const char* label; Category category; };
        static const Entry entries[] = {
            { "General", Category::General },
            { "Build", Category::Build },
            { "Physics", Category::Physics },
        };
        for (const Entry& e : entries)
            if (ImGui::Selectable(e.label, m_Category == e.category))
                m_Category = e.category;
        ImGui::EndChild();

        ImGui::SameLine();

        // Right : the selected category's settings.
        ImGui::BeginChild("##Settings", ImVec2(0, 0), ImGuiChildFlags_Borders);
        switch (m_Category)
        {
            case Category::General: DrawGeneralCategory(); break;
            case Category::Build:   DrawBuildCategory();   break;
            case Category::Physics: DrawPhysicsCategory(); break;
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void ProjectSettingsPanel::DrawGeneralCategory()
    {
        auto project = GetEngine().GetCurrentProject();

        ImGui::SeparatorText("Project");

        if (BeginRow("Name", "Project name (read-only, set when the project is created)."))
        {
            ImGui::BeginDisabled();
            ImGui::InputText("##Name", const_cast<char*>(project->name.c_str()), project->name.size() + 1, ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
        }

        if (BeginRow("Version", "Major.Minor.Patch"))
        {
            int version[3] = { project->versionMajor, project->versionMinor, project->versionPatch };
            if (ImGui::InputInt3("##Version", version))
            {
                project->versionMajor = std::max(0, version[0]);
                project->versionMinor = std::max(0, version[1]);
                project->versionPatch = std::max(0, version[2]);
                m_Dirty = true;
            }
        }

        ImGui::SeparatorText("Paths");

        struct PathRow { const char* label; std::string value; };
        const PathRow rows[] = {
            { "Project Root", project->GetProjectRoot().full },
            { "Resources", project->GetProjectResourcesPath().full },
            { "Plugins", project->GetPluginsFolderPath().full },
            { "Asset Database", project->GetAssetDatabasePath().full },
        };
        for (const PathRow& row : rows)
        {
            if (!BeginRow(row.label))
                continue;
            std::string id = std::string("##") + row.label;
            std::string value = row.value;
            ImGui::BeginDisabled();
            ImGui::InputText(id.c_str(), value.data(), value.size() + 1, ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
        }
    }

    void ProjectSettingsPanel::DrawBuildCategory()
    {
        auto* build = GetEngine().GetCurrentProject()->GetBuildSettings();

        ImGui::SeparatorText("Levels in Build");
        ImGui::TextDisabled("Index 0 is the level loaded when the game starts.");

        int moveFrom = -1, moveTo = -1, remove = -1;

        for (int i = 0; i < (int)build->buildIndex.size(); i++)
        {
            const std::string& path = build->buildIndex[i].full;
            if (!Matches(path.c_str()))
                continue;

            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", i);
            ImGui::SameLine(40.0f);
            ImGui::TextUnformatted(path.c_str());

            const float buttonsWidth = 3 * ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().ItemSpacing.x;
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - buttonsWidth);

            ImGui::BeginDisabled(i == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { moveFrom = i; moveTo = i - 1; }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(i == (int)build->buildIndex.size() - 1);
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) { moveFrom = i; moveTo = i + 2; }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("X", ImVec2(ImGui::GetFrameHeight(), 0))) remove = i;
            ImGui::PopID();
        }

        if (moveFrom >= 0)
        {
            build->ChangeBuildIndex(build->buildIndex[moveFrom], moveTo);
            m_Dirty = true;
        }
        if (remove >= 0)
        {
            build->buildIndex.erase(build->buildIndex.begin() + remove);
            m_Dirty = true;
        }

        if (build->buildIndex.empty())
            ImGui::TextDisabled("No levels in the build.");

        ImGui::Separator();
        if (ImGui::Button("Add Open Level"))
        {
            if (auto* level = GetEngine().GetLevelManager()->GetLevelAt(0))
            {
                size_t before = build->buildIndex.size();
                build->AddToBuildSettings(level->GetPath());
                m_Dirty |= build->buildIndex.size() != before;
            }
        }
    }

    void ProjectSettingsPanel::DrawPhysicsCategory()
    {
        auto* physics = GetEngine().GetCurrentProject()->GetPhysicsSettings();

        ImGui::SeparatorText("Simulation");

        if (BeginRow("Gravity", "Acceleration applied to every dynamic body, in world units per second squared."))
        {
            if (ImGui::DragFloat3("##Gravity", &physics->gravity.x, 0.05f))
            {
                GetEngine().GetPhysicsManager()->SetGravity(physics->gravity);
                m_Dirty = true;
            }
        }

        if (BeginRow("Fixed Time Step", "Time one simulation step covers, in seconds. Lower is more accurate but costs more steps per second."))
        {
            float hz = 1.0f / physics->fixedTimeStep;
            if (ImGui::DragFloat("##FixedStep", &physics->fixedTimeStep, 0.0005f, 0.001f, 0.1f, "%.4f s"))
            {
                physics->fixedTimeStep = std::clamp(physics->fixedTimeStep, 0.001f, 0.1f);
                physics->maxAccumulatedTime = std::max(physics->maxAccumulatedTime, physics->fixedTimeStep);
                GetEngine().GetTimeManager()->SetFixedDeltaTime(physics->fixedTimeStep);
                GetEngine().GetTimeManager()->SetMaxAccumulatedTime(physics->maxAccumulatedTime);
                m_Dirty = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%.1f steps per second", hz);
        }

        if (BeginRow("Max Accumulated Time", "Cap on the real time a single frame may contribute to the simulation. Bounds how far it tries to catch up after a stall."))
        {
            if (ImGui::DragFloat("##MaxAccum", &physics->maxAccumulatedTime, 0.005f, physics->fixedTimeStep, 2.0f, "%.3f s"))
            {
                physics->maxAccumulatedTime = std::max(physics->maxAccumulatedTime, physics->fixedTimeStep);
                GetEngine().GetTimeManager()->SetMaxAccumulatedTime(physics->maxAccumulatedTime);
                m_Dirty = true;
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Reset to Defaults"))
        {
            *physics = Projects::PhysicsSettings{};
            GetEngine().GetPhysicsManager()->SetGravity(physics->gravity);
            GetEngine().GetTimeManager()->SetFixedDeltaTime(physics->fixedTimeStep);
            GetEngine().GetTimeManager()->SetMaxAccumulatedTime(physics->maxAccumulatedTime);
            m_Dirty = true;
        }
    }
}

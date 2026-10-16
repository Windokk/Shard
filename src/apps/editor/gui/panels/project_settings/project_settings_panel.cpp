#include "project_settings_panel.hpp"

#include "engine/world/engine.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/frontend/pipelines/render_pipeline.hpp"
#include "engine/assets/project/project.hpp"
#include "engine/assets/serialization/project/project_serializer.hpp"
#include "engine/world/time_manager.hpp"
#include "engine/core/config/cvar.hpp"

#include "apps/editor/gui/IconsLucide.h"
#include "apps/editor/gui/notifications.hpp"
#include "apps/editor/gui/popups.hpp"
#include "apps/editor/gui/dragdrop/asset_drag_drop.hpp"
#include "apps/editor/gui/panels/asset_browser/asset_operations.hpp"

#include "imgui/imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_set>

using namespace Shard::Engine;
using Shard::Engine::Core::GetEngine;
using Shard::Engine::Core::CVarRegistry;
using Shard::Engine::Core::CVarBase;
using Shard::Engine::Core::ConfigLayer;

namespace Shard::Editor::GUI{

    namespace {

        constexpr const char* kBuildWorldPayload = "SHARD_BUILD_WORLD";

        const ImVec4 kWarningColor(0.95f, 0.70f, 0.30f, 1.0f);

        bool IsWorldAsset(const std::string& nameInProject)
        {
            for (const auto& [id, info] : GetEngine().GetAssetIDManager()->AssetIDMap)
                if (info && info->baseInfos.nameInProject == nameInProject)
                    return info->baseInfos.type == Filesystem::Type::T_WORLD;
            return false;
        }

        /// The open world as the build settings name it (its name in the project), empty when none is open
        std::string OpenWorldName()
        {
            Worlds::World* world = GetEngine().GetWorldManager()->GetWorldAt(0);
            if (!world)
                return {};
            return GetEngine().GetFileManager()->GetFileInfos(world->GetPath()).nameInProject;
        }

        void Tooltip(const char* text)
        {
            if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                return;
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    ProjectSettingsPanel::ProjectSettingsPanel()
    {
        m_View.SetCategories({
            { "General", ICON_LC_INFO, [this]{ DrawGeneralCategory(); } },
            { "Build", ICON_LC_PACKAGE, [this]{ DrawBuildCategory(); } },
            { "Physics", ICON_LC_ATOM, [this]{ DrawPhysicsCategory(); } },
            { "Rendering", ICON_LC_MONITOR, [this]{ DrawRenderingCategory(); } },
            { "Console Variables", ICON_LC_TERMINAL, [this]{ DrawConsoleVariablesCategory(); } },
        });
    }

    // ---- Saving ----

    bool ProjectSettingsPanel::IsDirty() const
    {
        auto project = GetEngine().GetCurrentProject();
        if (!project)
            return false;
        // Compared with what the file holds : a change made anywhere (a world added to the build from the menu) counts too
        return m_CVarsDirty || project->HasUnsavedChanges();
    }

    std::string ProjectSettingsPanel::GetProjectConfigPath() const
    {
        auto project = GetEngine().GetCurrentProject();
        return (project->GetProjectRoot() / std::string("project.cfg")).GetNativePath();
    }

    bool ProjectSettingsPanel::WriteProjectConfig() const
    {
        CVarRegistry& registry = CVarRegistry::Global();

        // Only the variables the registry lists (the hidden ones, and those of modules not loaded, are not written back)
        std::string text = "# Console variables the project sets (name = value), read when the engine starts.\n"
                           "# The user's user.cfg, the command line and the console still win over them. Edited by Project Settings.\n";
        size_t count = 0;
        for (CVarBase* cvar : registry.List())
        {
            if (auto value = registry.GetLayerValue(ConfigLayer::Project, cvar->Name()))
            {
                text += cvar->Name() + " = " + *value + "\n";
                count++;
            }
        }

        const std::string path = GetProjectConfigPath();
        if (count == 0 && !std::filesystem::exists(path))
            return true;

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;
        file << text;
        return static_cast<bool>(file);
    }

    bool ProjectSettingsPanel::Save()
    {
        auto project = GetEngine().GetCurrentProject();
        if (!project)
            return true;

        if (!project->Save(GetEngine().GetSettings().project))
        {
            Notifications::Error("Could not write %s", GetEngine().GetSettings().project.c_str());
            return false;
        }

        if (m_CVarsDirty && !WriteProjectConfig())
        {
            Notifications::Error("Could not write %s", GetProjectConfigPath().c_str());
            return false;
        }

        m_CVarsDirty = false;
        Notifications::Success("Project settings saved");
        return true;
    }

    void ProjectSettingsPanel::ApplyToEngine()
    {
        auto project = GetEngine().GetCurrentProject();
        Projects::PhysicsSettings* physics = project->GetPhysicsSettings();
        Projects::RenderingSettings* rendering = project->GetRenderingSettings();

        GetEngine().GetPhysicsManager()->SetGravity(physics->gravity);
        GetEngine().GetTimeManager()->SetFixedDeltaTime(physics->fixedTimeStep);
        GetEngine().GetTimeManager()->SetMaxAccumulatedTime(physics->maxAccumulatedTime);

        GetEngine().GetRenderer()->SetOcclusionCullingEnabled(rendering->occlusionCulling);
        GetEngine().GetRenderer()->SetMultisampleCount(rendering->msaaSamples);
        GetEngine().GetRenderer()->SetRenderPipeline(rendering->renderPipeline);
    }

    void ProjectSettingsPanel::Revert()
    {
        auto project = GetEngine().GetCurrentProject();
        if (!project)
            return;

        auto saved = Serialization::DeserializeProject(Filesystem::Path(GetEngine().GetSettings().project));
        if (!saved)
        {
            Notifications::Error("Could not read the project file back : nothing reverted");
            return;
        }

        *project->GetBuildSettings() = *saved->GetBuildSettings();
        *project->GetPhysicsSettings() = *saved->GetPhysicsSettings();
        *project->GetRenderingSettings() = *saved->GetRenderingSettings();
        ApplyToEngine();

        // The console variables : what project.cfg says again
        CVarRegistry& registry = CVarRegistry::Global();
        registry.ClearLayer(ConfigLayer::Project);
        if (std::filesystem::exists(GetProjectConfigPath()))
            registry.LoadFile(ConfigLayer::Project, GetProjectConfigPath());

        project->MarkSaved();
        m_CVarsDirty = false;
        Notifications::Info("Project settings reverted to the saved ones");
    }

    // ---- Window ----

    void ProjectSettingsPanel::Draw(bool* open)
    {
        auto project = GetEngine().GetCurrentProject();
        const bool dirty = IsDirty();

        // The same window whatever its title says (### keeps its identity)
        ImGui::SetNextWindowSize(ImVec2(820, 500), ImGuiCond_FirstUseEver);
        const bool wasOpen = *open;
        const bool visible = ImGui::Begin(dirty ? "Project Settings *###ProjectSettings" : "Project Settings###ProjectSettings", open);

        // Closing with unsaved changes asks first ; the window stays until the answer
        if (wasOpen && !*open && dirty)
        {
            *open = true;
            Popups::ConfirmUnsavedChanges("Project Settings",
                [this, open]{ if (Save()) *open = false; },
                [this, open]{ Revert(); *open = false; });
        }

        if (!visible)
        {
            ImGui::End();
            return;
        }

        if (!project)
        {
            ImGui::TextDisabled("No project loaded");
            ImGui::End();
            return;
        }

        // Top bar : search, revert, save
        const char* revertLabel = ICON_LC_UNDO_2 " Revert";
        const char* saveLabel = ICON_LC_SAVE " Save";
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttonsWidth = ImGui::CalcTextSize(revertLabel).x + ImGui::CalcTextSize(saveLabel).x
                                 + style.FramePadding.x * 4.0f + style.ItemSpacing.x;
        m_View.DrawSearchBar(buttonsWidth);

        ImGui::SameLine();
        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button(revertLabel))
            Revert();
        Tooltip("Reload the project file and project.cfg, dropping the changes not saved.");

        ImGui::SameLine();
        if (dirty)
            ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonHovered]);
        if (ImGui::Button(saveLabel))
            Save();
        if (dirty)
            ImGui::PopStyleColor();
        ImGui::EndDisabled();
        Tooltip("Write the project file (and project.cfg).");

        if (dirty)
            ImGui::TextColored(kWarningColor, ICON_LC_TRIANGLE_ALERT " Unsaved changes. They already apply to the editor.");
        else
            ImGui::TextDisabled("%s", GetEngine().GetSettings().project.c_str());

        m_View.DrawBody();

        ImGui::End();
    }

    // ---- General ----

    void ProjectSettingsPanel::PathRow(const char* label, const std::string& path, const char* tooltip)
    {
        if (!m_View.BeginRow(label, tooltip))
            return;

        const float buttonWidth = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(-(buttonWidth + ImGui::GetStyle().ItemSpacing.x));
        std::string value = path;
        const std::string id = std::string("##") + label;
        ImGui::InputText(id.c_str(), value.data(), value.size() + 1, ImGuiInputTextFlags_ReadOnly);

        ImGui::SameLine();
        const std::string native = Filesystem::Path(path).GetNativePath();
        const bool exists = !path.empty() && std::filesystem::exists(native);
        ImGui::BeginDisabled(!exists);
        const std::string buttonId = std::string(ICON_LC_FOLDER_OPEN "##Reveal") + label;
        if (ImGui::Button(buttonId.c_str(), ImVec2(buttonWidth, 0)))
            AssetOperations::RevealInFileExplorer(native, !std::filesystem::is_directory(native));
        ImGui::EndDisabled();
        Tooltip(exists ? "Show in the file explorer" : "Does not exist");
    }

    void ProjectSettingsPanel::DrawGeneralCategory()
    {
        auto project = GetEngine().GetCurrentProject();

        m_View.Section("Project");

        if (m_View.BeginRow("Name", "The project's name : its file's name, without the extension."))
        {
            std::string name = project->name;
            ImGui::InputText("##Name", name.data(), name.size() + 1, ImGuiInputTextFlags_ReadOnly);
        }

        // Read only : the engine only opens a project whose version is its own, so a project saved with another one could
        // not be opened again (nor reverted to)
        if (m_View.BeginRow("Version", "Major.Minor.Patch : the version of the engine the project is made for. The engine only opens a project whose version is its own."))
        {
            std::string version = std::to_string(project->versionMajor) + "." + std::to_string(project->versionMinor) + "." + std::to_string(project->versionPatch);
            ImGui::InputText("##Version", version.data(), version.size() + 1, ImGuiInputTextFlags_ReadOnly);
        }

        m_View.Section("Paths");

        PathRow("Project File", GetEngine().GetSettings().project, "The file these settings are saved to.");
        PathRow("Project Root", project->GetProjectRoot().full, "The folder of the project file : every path of the project is relative to it.");
        PathRow("Resources", project->GetProjectResourcesPath().full, "Where the assets live (what the asset browser shows).");
        PathRow("Plugins", project->GetPluginsFolderPath().full, "Where the game modules of the project are looked for.");
        PathRow("Asset Database", project->GetAssetDatabasePath().full, "The IDs of the assets and what each one depends on.");
        PathRow("Console Variables File", GetProjectConfigPath(), "project.cfg : the console variables the project sets (see Console Variables).");
    }

    // ---- Build ----

    void ProjectSettingsPanel::DrawBuildCategory()
    {
        Projects::BuildSettings* build = GetEngine().GetCurrentProject()->GetBuildSettings();

        // Every world asset of the project, to offer the ones not in the build yet and to flag entries that point at nothing
        m_WorldAssets.clear();
        for (const auto& [id, info] : GetEngine().GetAssetIDManager()->AssetIDMap)
            if (info && info->baseInfos.type == Filesystem::Type::T_WORLD && !info->baseInfos.nameInProject.empty())
                m_WorldAssets.push_back(info->baseInfos.nameInProject);
        std::sort(m_WorldAssets.begin(), m_WorldAssets.end());
        const std::unordered_set<std::string> known(m_WorldAssets.begin(), m_WorldAssets.end());

        const std::string openWorld = OpenWorldName();

        m_View.Section("Worlds in Build");

        // The block shows when the search names one of the worlds too
        std::string keywords = "worlds in build index start";
        for (const Filesystem::Path& world : build->buildIndex)
            keywords += " " + world.full;
        if (!m_View.BeginBlock(keywords.c_str()))
            return;

        ImGui::TextDisabled("The world at index 0 is the one the game starts with. Drag the rows to reorder them.");

        int moveFrom = -1, moveTo = -1, remove = -1;

        const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuter
                                         | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##BuildWorlds", 3, tableFlags))
        {
            const float buttons = 3 * ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().ItemSpacing.x;
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.0f);
            ImGui::TableSetupColumn("World", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed, buttons);

            const int count = static_cast<int>(build->buildIndex.size());
            for (int i = 0; i < count; i++)
            {
                const std::string& name = build->buildIndex[i].full;

                ImGui::PushID(i);
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (i == 0)
                    ImGui::Text(ICON_LC_PLAY " %d", i);
                else
                    ImGui::Text("%d", i);

                ImGui::TableNextColumn();
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
                ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, ImGui::GetFrameHeight()));
                ImGui::PopStyleVar();

                // Reordering : drag a row onto another
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload(kBuildWorldPayload, &i, sizeof(int));
                    ImGui::Text("%d  %s", i, name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kBuildWorldPayload))
                    {
                        moveFrom = *static_cast<const int*>(payload->Data);
                        // Dropped on a row : takes that row's place
                        moveTo = moveFrom < i ? i + 1 : i;
                    }
                    ImGui::EndDragDropTarget();
                }

                if (i == 0)
                    Tooltip("Loaded when the game starts.");

                if (!known.count(name))
                {
                    ImGui::SameLine();
                    ImGui::TextColored(kWarningColor, ICON_LC_TRIANGLE_ALERT " not found");
                    Tooltip("No world asset has this name in the asset database : the build cannot load it.");
                }
                if (name == openWorld)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(open)");
                }

                ImGui::TableNextColumn();
                ImGui::BeginDisabled(i == 0);
                if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { moveFrom = i; moveTo = i - 1; }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(i == count - 1);
                if (ImGui::ArrowButton("##down", ImGuiDir_Down)) { moveFrom = i; moveTo = i + 2; }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button(ICON_LC_X "##remove", ImVec2(ImGui::GetFrameHeight(), 0)))
                    remove = i;
                Tooltip("Remove from the build");

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        if (build->buildIndex.empty())
            ImGui::TextColored(kWarningColor, ICON_LC_TRIANGLE_ALERT " No world in the build : the game has nothing to start with.");

        if (moveFrom >= 0 && moveFrom < static_cast<int>(build->buildIndex.size()))
            build->ChangeBuildIndex(build->buildIndex[moveFrom], moveTo);
        else if (remove >= 0)
            build->buildIndex.erase(build->buildIndex.begin() + remove);

        // Adding : drop world assets from the asset browser, pick one, or take the open one
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::BeginDisabled();
        ImGui::Button(ICON_LC_PLUS " Drop world assets here", ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 1.6f));
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if (ImGui::BeginDragDropTarget())
        {
            for (const std::string& name : DragDrop::AcceptAssetDragDropPayload())
            {
                if (IsWorldAsset(name))
                    build->AddToBuildSettings(Filesystem::Path(name));
                else
                    Notifications::Warning("\"%s\" is not a world", name.c_str());
            }
            ImGui::EndDragDropTarget();
        }

        std::vector<const std::string*> addable;
        for (const std::string& name : m_WorldAssets)
            if (build->GetWorldBuildIndex(Filesystem::Path(name)) < 0)
                addable.push_back(&name);

        const float addOpenWidth = ImGui::CalcTextSize("Add Open World").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(-(addOpenWidth + ImGui::GetStyle().ItemSpacing.x));
        ImGui::BeginDisabled(addable.empty());
        if (ImGui::BeginCombo("##AddWorld", addable.empty() ? "Every world is in the build" : "Add a world..."))
        {
            for (const std::string* name : addable)
                if (ImGui::Selectable(name->c_str()))
                    build->AddToBuildSettings(Filesystem::Path(*name));
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(openWorld.empty() || build->GetWorldBuildIndex(Filesystem::Path(openWorld)) >= 0);
        if (ImGui::Button("Add Open World"))
            build->AddToBuildSettings(Filesystem::Path(openWorld));
        ImGui::EndDisabled();
    }

    // ---- Physics ----

    void ProjectSettingsPanel::DrawPhysicsCategory()
    {
        Projects::PhysicsSettings* physics = GetEngine().GetCurrentProject()->GetPhysicsSettings();
        const Projects::PhysicsSettings defaults{};
        Time::TimeManager* time = GetEngine().GetTimeManager();

        m_View.Section("World");

        bool reset = false;
        if (m_View.BeginRow("Gravity", "Acceleration applied to every dynamic body, in world units per second squared. Earth's is 9.81 for a project in metres.",
                            physics->gravity != defaults.gravity, &reset))
        {
            if (reset)
                physics->gravity = defaults.gravity;
            if (ImGui::DragFloat3("##Gravity", &physics->gravity.x, 0.05f, -1000.0f, 1000.0f, "%.2f") || reset)
                GetEngine().GetPhysicsManager()->SetGravity(physics->gravity);
        }

        m_View.Section("Simulation");

        if (m_View.DragFloat("Fixed Time Step", "Time one simulation step covers, in seconds. Lower is more accurate but costs more steps per second.",
                             physics->fixedTimeStep, defaults.fixedTimeStep, 0.0005f, 0.001f, 0.1f, "%.4f s"))
        {
            physics->maxAccumulatedTime = std::max(physics->maxAccumulatedTime, physics->fixedTimeStep);
            time->SetFixedDeltaTime(physics->fixedTimeStep);
            time->SetMaxAccumulatedTime(physics->maxAccumulatedTime);
        }

        // The same setting, the way it is usually thought of
        if (m_View.BeginRow("Steps per Second", "1 / Fixed Time Step."))
        {
            float hz = 1.0f / physics->fixedTimeStep;
            if (ImGui::DragFloat("##StepsPerSecond", &hz, 0.5f, 10.0f, 1000.0f, "%.1f Hz", ImGuiSliderFlags_AlwaysClamp))
            {
                physics->fixedTimeStep = 1.0f / hz;
                physics->maxAccumulatedTime = std::max(physics->maxAccumulatedTime, physics->fixedTimeStep);
                time->SetFixedDeltaTime(physics->fixedTimeStep);
                time->SetMaxAccumulatedTime(physics->maxAccumulatedTime);
            }
        }

        if (m_View.DragFloat("Max Accumulated Time", "Cap on the real time a single frame may hand to the simulation. Bounds how far it tries to catch up after a stall.",
                             physics->maxAccumulatedTime, defaults.maxAccumulatedTime, 0.005f, physics->fixedTimeStep, 2.0f, "%.3f s"))
        {
            physics->maxAccumulatedTime = std::max(physics->maxAccumulatedTime, physics->fixedTimeStep);
            time->SetMaxAccumulatedTime(physics->maxAccumulatedTime);
        }

        if (m_View.BeginBlock("steps per frame catch up"))
        {
            const float maxSteps = physics->maxAccumulatedTime / physics->fixedTimeStep;
            ImGui::TextDisabled("At most %.0f steps in one frame after a stall.", std::floor(maxSteps));
        }
    }

    // ---- Rendering ----

    void ProjectSettingsPanel::DrawRenderingCategory()
    {
        Projects::RenderingSettings* rendering = GetEngine().GetCurrentProject()->GetRenderingSettings();
        const Projects::RenderingSettings defaults{};
        Rendering::Renderer* renderer = GetEngine().GetRenderer();

        m_View.Section("Pipeline");

        bool reset = false;
        if (m_View.BeginRow("Render Pipeline", "How the frames are built : a built-in type (forward+, deferred) or a .renderpipeline asset that configures one.",
                            rendering->renderPipeline != defaults.renderPipeline, &reset))
        {
            if (reset)
            {
                rendering->renderPipeline = defaults.renderPipeline;
                renderer->SetRenderPipeline(rendering->renderPipeline);
            }

            const std::vector<Rendering::RenderPipelineInfo> pipelines = Rendering::RenderPipelineRegistry::Get().List();
            const bool known = Rendering::RenderPipelineRegistry::Get().Has(rendering->renderPipeline);
            const std::string preview = known ? rendering->renderPipeline : rendering->renderPipeline + "  (missing)";

            if (ImGui::BeginCombo("##RenderPipeline", preview.c_str()))
            {
                for (const Rendering::RenderPipelineInfo& info : pipelines)
                {
                    const std::string label = info.path.empty() ? info.name : info.name + "  (" + info.type + ")";
                    if (ImGui::Selectable(label.c_str(), info.name == rendering->renderPipeline))
                    {
                        rendering->renderPipeline = info.name;
                        renderer->SetRenderPipeline(info.name);
                    }
                    if (!info.description.empty())
                        Tooltip(info.description.c_str());
                }
                ImGui::EndCombo();
            }
        }

        m_View.Section("Culling");

        if (m_View.Checkbox("Occlusion Culling", "The GPU skips drawing geometry that is hidden behind other geometry (Hi-Z occlusion culling, computed every frame in a compute shader). It never changes the image - only how much has to be drawn to produce it. Turn it off to compare, or to rule it out when chasing a rendering problem.",
                            rendering->occlusionCulling, defaults.occlusionCulling))
            renderer->SetOcclusionCullingEnabled(rendering->occlusionCulling);

        m_View.Section("Anti-aliasing");

        reset = false;
        if (m_View.BeginRow("MSAA Samples", "Samples per pixel of the viewport (multisample anti-aliasing). More samples smooth edges better but cost GPU time and memory ; the GPU's maximum caps the value.",
                            rendering->msaaSamples != defaults.msaaSamples, &reset))
        {
            if (reset)
            {
                rendering->msaaSamples = defaults.msaaSamples;
                renderer->SetMultisampleCount(rendering->msaaSamples);
            }

            static const uint32_t counts[] = { 2, 4, 8, 16 };
            const std::string preview = std::to_string(rendering->msaaSamples) + "x";
            if (ImGui::BeginCombo("##MSAASamples", preview.c_str()))
            {
                for (uint32_t count : counts)
                {
                    const std::string label = std::to_string(count) + "x";
                    if (ImGui::Selectable(label.c_str(), rendering->msaaSamples == count))
                    {
                        rendering->msaaSamples = count;
                        renderer->SetMultisampleCount(count);
                    }
                }
                ImGui::EndCombo();
            }
        }
    }

    // ---- Console variables ----

    void ProjectSettingsPanel::DrawConsoleVariablesCategory()
    {
        CVarRegistry& registry = CVarRegistry::Global();

        m_View.Section("project.cfg");

        std::vector<CVarBase*> shown;
        for (CVarBase* cvar : registry.List())
        {
            const bool set = registry.GetLayerValue(ConfigLayer::Project, cvar->Name()).has_value();
            if (m_ProjectCVarsOnly && !set)
                continue;
            if (m_View.IsSearching() && !m_View.GroupMatches()
                && !m_View.Matches(cvar->Name().c_str()) && !m_View.Matches(cvar->Description().c_str()))
                continue;
            shown.push_back(cvar);
        }

        if (m_View.IsSearching() && shown.empty())
            return;
        // Names the variables kept : the block shows whenever one of them matched
        std::string keywords = "console variables project.cfg";
        for (CVarBase* cvar : shown)
            keywords += " " + cvar->Name() + " " + cvar->Description();
        if (!m_View.BeginBlock(keywords.c_str()))
            return;

        ImGui::TextWrapped("The values the project gives the console variables, saved to project.cfg next to the project file. "
                           "The user's user.cfg, the command line and the console win over them. Some only apply when the engine starts.");
        ImGui::Checkbox("Only the ones the project sets", &m_ProjectCVarsOnly);

        const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuter
                                         | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
        const float height = std::max(ImGui::GetContentRegionAvail().y, ImGui::GetFrameHeightWithSpacing() * 8.0f);
        if (!ImGui::BeginTable("##ProjectCVars", 3, tableFlags, ImVec2(0, height)))
            return;

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Variable", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Project Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("In Use", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableHeadersRow();

        const ImVec4 setColor = ImGui::GetStyle().Colors[ImGuiCol_Text];
        const ImVec4 unsetColor = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];

        for (CVarBase* cvar : shown)
        {
            const std::string& name = cvar->Name();
            const std::optional<std::string> projectValue = registry.GetLayerValue(ConfigLayer::Project, name);
            const ConfigLayer winning = registry.WinningLayer(name);

            ImGui::PushID(name.c_str());
            ImGui::TableNextRow();

            // Name, with what it is for
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(projectValue ? setColor : unsetColor, "%s", name.c_str());
            {
                std::string tip = cvar->Description().empty() ? std::string("(no description)") : cvar->Description();
                tip += "\n\nType : " + std::string(cvar->TypeName()) + "\nDefault : " + cvar->DefaultString();
                if (Shard::Engine::Core::HasFlag(cvar->Flags(), Shard::Engine::Core::CVarFlags::ReadOnly))
                    tip += "\nRead-only : only config files set it.";
                Tooltip(tip.c_str());
            }

            // The project's value : edited in place, the arrow takes it out of project.cfg
            ImGui::TableNextColumn();
            const std::string shownValue = projectValue ? *projectValue : cvar->DefaultString();
            // Room for the reset arrow on the right when there is one (-FLT_MIN : the whole cell, a width of -0 would be none)
            ImGui::SetNextItemWidth(projectValue ? -(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x) : -FLT_MIN);

            if (std::strcmp(cvar->TypeName(), "bool") == 0)
            {
                bool value = shownValue == "true";
                if (ImGui::Checkbox("##value", &value))
                {
                    registry.SetLayerValue(ConfigLayer::Project, name, value ? "true" : "false");
                    m_CVarsDirty = true;
                }
            }
            else
            {
                const bool editing = m_EditingCVar == name;
                char scratch[256];
                std::snprintf(scratch, sizeof(scratch), "%s", shownValue.c_str());
                char* buffer = editing ? m_CVarBuffer : scratch;

                if (!projectValue)
                    ImGui::PushStyleColor(ImGuiCol_Text, unsetColor);
                ImGui::InputText("##value", buffer, sizeof(m_CVarBuffer));
                if (!projectValue)
                    ImGui::PopStyleColor();

                if (ImGui::IsItemActivated())
                {
                    m_EditingCVar = name;
                    std::snprintf(m_CVarBuffer, sizeof(m_CVarBuffer), "%s", shownValue.c_str());
                }
                if (ImGui::IsItemDeactivated() && m_EditingCVar == name)
                {
                    if (ImGui::IsItemDeactivatedAfterEdit() && shownValue != m_CVarBuffer)
                    {
                        std::string error;
                        if (registry.SetLayerValue(ConfigLayer::Project, name, m_CVarBuffer, &error))
                            m_CVarsDirty = true;
                        else
                            Notifications::Error("%s : %s", name.c_str(), error.c_str());
                    }
                    m_EditingCVar.clear();
                }
            }

            if (projectValue)
            {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.30f, 1.0f));
                if (ImGui::Button(ICON_LC_ROTATE_CCW "##clear", ImVec2(ImGui::GetFrameHeight(), 0)))
                {
                    registry.ClearLayerValue(ConfigLayer::Project, name);
                    m_CVarsDirty = true;
                }
                ImGui::PopStyleColor(2);
                Tooltip("Take it out of project.cfg");
            }

            // What the engine uses now, and who decided it
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const std::string current = cvar->ToString();
            if (winning > ConfigLayer::Project)
            {
                ImGui::TextColored(kWarningColor, "%s  (%s)", current.c_str(), Shard::Engine::Core::ConfigLayerName(winning));
                Tooltip("Another layer wins over the project's value : user.cfg, the command line or the console.");
            }
            else
            {
                ImGui::TextDisabled("%s  (%s)", current.c_str(), Shard::Engine::Core::ConfigLayerName(winning));
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

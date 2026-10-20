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
            { "Tags", ICON_LC_TAGS, [this]{ DrawTagsCategory(); } },
            { "Console Variables", ICON_LC_TERMINAL, [this]{ DrawConsoleVariablesCategory(); } },
        });
    }

    // ---- Saving ----

    bool ProjectSettingsPanel::IsDirty() const
    {
        auto project = GetEngine().GetCurrentProject();
        if (!project)
            return false;
        if (m_CVarsDirty)
            return true;

        // Compared with what the file holds : a change made anywhere (a world added to the build from the menu) counts too.
        // That serializes the whole project, so not on every frame : a few times a second is plenty for a "unsaved" label
        const double now = ImGui::GetTime();
        if (now - m_LastDirtyCheck >= 0.25)
        {
            m_ProjectDirty = project->HasUnsavedChanges();
            m_LastDirtyCheck = now;
        }
        return m_ProjectDirty;
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
        GetEngine().GetRenderer()->SetShadowSettings(rendering->shadows);
        GetEngine().GetRenderer()->SetRenderPipeline(rendering->renderPipeline);

        Projects::ApplyTagSettings(*project->GetTagSettings());
    }

    void ProjectSettingsPanel::Revert()
    {
        auto project = GetEngine().GetCurrentProject();
        m_LastDirtyCheck = -1.0;
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
        *project->GetTagSettings() = *saved->GetTagSettings();
        ApplyToEngine();

        // The console variables : what project.cfg says again
        CVarRegistry& registry = CVarRegistry::Global();
        registry.ClearLayer(ConfigLayer::Project);
        if (std::filesystem::exists(GetProjectConfigPath()))
            registry.LoadFile(ConfigLayer::Project, GetProjectConfigPath());

        project->MarkSaved();
        m_LastDirtyCheck = -1.0;
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
        PathRow("Plugins", project->GetPluginsFolderPath().full, "Where the plugins and datapacks of the project are looked for (see the Plugins panel).");
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

        // ---- Shadows ----

        Projects::ShadowSettings& shadows = rendering->shadows;
        const Projects::ShadowSettings& shadowDefaults = defaults.shadows;
        auto applyShadows = [&] { renderer->SetShadowSettings(shadows); };

        // The side of a shadow map : a power of two from 512, the cost grows with its square
        static const char* const resolutionLabels[] = { "512", "1024", "2048", "4096" };
        static const uint32_t resolutions[] = { 512, 1024, 2048, 4096 };
        auto resolutionRow = [&](const char* label, const char* tooltip, uint32_t& value, uint32_t defaultValue) {
            auto indexOf = [](uint32_t v) {
                int best = 0;
                for (int i = 0; i < 4; i++)
                    if (resolutions[i] <= v)
                        best = i;
                return best;
            };
            int index = indexOf(value);
            if (m_View.Combo(label, tooltip, index, indexOf(defaultValue), resolutionLabels, 4))
            {
                value = resolutions[index];
                applyShadows();
            }
        };
        auto floatRow = [&](const char* label, const char* tooltip, float& value, float defaultValue, float speed, float min, float max, const char* format = "%.3f") {
            if (m_View.DragFloat(label, tooltip, value, defaultValue, speed, min, max, format))
                applyShadows();
        };
        auto countRow = [&](const char* label, const char* tooltip, uint32_t& value, uint32_t defaultValue, int min, int max) {
            int v = int(value);
            if (m_View.DragInt(label, tooltip, v, int(defaultValue), 0.1f, min, max))
            {
                value = uint32_t(std::clamp(v, min, max));
                applyShadows();
            }
        };
        auto boolRow = [&](const char* label, const char* tooltip, bool& value, bool defaultValue) {
            if (m_View.Checkbox(label, tooltip, value, defaultValue))
                applyShadows();
        };

        m_View.Section("Shadows");

        boolRow("Enable Shadows", "Draws the shadows of the lights. Off, every light is unshadowed (the shadow maps are still rendered : this is for comparing).",
                shadows.enabled, shadowDefaults.enabled);

        m_View.Section("Directional Shadows");

        resolutionRow("Resolution", "Side, in texels, of each cascade shadow map of a directional light (a sun). Higher is sharper, and costs one such map per cascade in video memory.",
                      shadows.directionalResolution, shadowDefaults.directionalResolution);
        floatRow("Distance", "How far from the camera the sun's shadows reach, in world units. 0 follows the camera's far plane. The cascades are fitted to this range : a shorter one gives sharper shadows near the camera, and nothing beyond it is shadowed.",
                 shadows.directionalDistance, shadowDefaults.directionalDistance, 1.0f, 0.0f, 100000.0f, "%.1f");
        if (DrawCascadeEditor(shadows, shadowDefaults))
            applyShadows();
        boolRow("Stabilize Cascades", "Snaps each cascade to the texels of its map so the shadows do not shimmer when the camera moves, at the cost of a slightly looser fit.",
                shadows.stabilizeCascades, shadowDefaults.stabilizeCascades);
        floatRow("Caster Range", "How far from a cascade, towards the sun, the shadow casters are still drawn, in world units. A caster further than this casts no shadow on it (a high ceiling over a close cascade).",
                 shadows.casterRange, shadowDefaults.casterRange, 5.0f, 1.0f, 100000.0f, "%.0f");
        boolRow("Double-sided Casters", "Draws both faces of the geometry into the sun's shadow maps. Levels are rarely made of closed volumes (a roof or a wall is often one surface) : drawing only the faces turned away from the sun lets its light through the edges of such surfaces, as a bright line along a wall or around a shadow. Turn it off for a little less cost on closed meshes.",
                shadows.directionalDoubleSided, shadowDefaults.directionalDoubleSided);
        boolRow("Conservative Rasterization", "Draws every texel a caster touches into the sun's shadow maps, not only the ones whose centre it covers. A gap thinner than a texel (the joint between two pieces of a model, like the cubies of the Rubik's cube) would otherwise let a thin line of light through. Needs OpenGL and a GPU with GL_NV_conservative_raster ; ignored otherwise.",
                shadows.directionalConservativeRaster, shadowDefaults.directionalConservativeRaster);
        floatRow("Normal Bias", "How far the lit point is moved along the surface's normal before it is looked up in the map, in texels (more on a surface that grazes the light). Fights shadow acne ; too high detaches the shadow from its caster.",
                 shadows.directionalNormalBias, shadowDefaults.directionalNormalBias, 0.01f, 0.0f, 16.0f, "%.2f");
        floatRow("Depth Bias", "Constant depth offset of the comparison, in texels. Fights shadow acne ; too high lets light leak at contact points.",
                 shadows.directionalDepthBias, shadowDefaults.directionalDepthBias, 0.01f, 0.0f, 16.0f, "%.2f");
        floatRow("Slope Bias", "Scale of the receiver plane bias, which follows the slope of the surface across the filter (0 : none, 1 : exact). Keeps a wide filter from shadowing slanted surfaces with themselves.",
                 shadows.directionalSlopeBias, shadowDefaults.directionalSlopeBias, 0.01f, 0.0f, 4.0f, "%.2f");
        countRow("Filter Radius", "The filter takes (2 * radius + 1)^2 taps : 0 is a single hardware filtered tap, 2 a 5 x 5 kernel. More taps give softer edges and cost more.",
                 shadows.directionalFilterRadius, shadowDefaults.directionalFilterRadius, 0, 4);
        floatRow("Filter Spread", "Distance between two taps of the filter, in texels. Wider gives softer edges, and visible bands when the taps are far apart.",
                 shadows.directionalFilterSpread, shadowDefaults.directionalFilterSpread, 0.01f, 0.1f, 8.0f, "%.2f");

        m_View.Section("Spot Shadows");

        resolutionRow("Resolution", "Side, in texels, of the shadow map of a spot light.", shadows.spotResolution, shadowDefaults.spotResolution);
        floatRow("Depth Bias", "Depth bias that grows with the slope of the surface to the light, in depth units of the map.",
                 shadows.spotDepthBias, shadowDefaults.spotDepthBias, 0.00001f, 0.0f, 0.1f, "%.5f");
        floatRow("Minimum Depth Bias", "Smallest depth bias, on a surface that faces the light.",
                 shadows.spotMinDepthBias, shadowDefaults.spotMinDepthBias, 0.000005f, 0.0f, 0.1f, "%.6f");
        countRow("Filter Radius", "The filter takes (2 * radius + 1)^2 taps : 0 is a single tap, 2 a 5 x 5 kernel.",
                 shadows.spotFilterRadius, shadowDefaults.spotFilterRadius, 0, 4);
        floatRow("Filter Spread", "Distance between two taps of the filter, in texels.",
                 shadows.spotFilterSpread, shadowDefaults.spotFilterSpread, 0.01f, 0.1f, 8.0f, "%.2f");

        m_View.Section("Point Shadows");

        resolutionRow("Resolution", "Side, in texels, of each of the six faces of the cube shadow map of a point light.", shadows.pointResolution, shadowDefaults.pointResolution);
        floatRow("Depth Bias", "Depth bias, in world units. Fights shadow acne ; too high lets light leak at contact points.",
                 shadows.pointDepthBias, shadowDefaults.pointDepthBias, 0.005f, 0.0f, 5.0f, "%.3f");
        countRow("Filter Samples", "Taps of the filter (1 to 20), spread on a disc around the lookup direction. 1 is a hard edge.",
                 shadows.pointFilterSamples, shadowDefaults.pointFilterSamples, 1, 20);
        floatRow("Filter Radius", "Radius of that disc, in world units at the surface : the softness of the shadow's edge.",
                 shadows.pointFilterRadius, shadowDefaults.pointFilterRadius, 0.005f, 0.0f, 2.0f, "%.3f");
    }

    bool ProjectSettingsPanel::DrawCascadeEditor(Projects::ShadowSettings& shadows, const Projects::ShadowSettings& defaults)
    {
        bool changed = false;
        bool reset = false;
        const bool modified = shadows.cascadeSplits != defaults.cascadeSplits || shadows.cascadeColors != defaults.cascadeColors;
        if (!m_View.BeginRow("Cascades", "How the range of the sun's shadows is cut into cascades, nearest to the camera on the left. Drag a handle to move the split between two cascades, right-click one to merge its two cascades. Each cascade has a color in the \"Shadow Cascades\" debug view (click a cascade to edit it). Fewer cascades are cheaper, more give sharper shadows ; each sun takes one shadow map slot per cascade, and the slots are limited, so with more cascades fewer suns can cast shadows.",
                             modified, &reset))
            return false;

        if (reset)
        {
            shadows.cascadeSplits = defaults.cascadeSplits;
            shadows.cascadeColors = defaults.cascadeColors;
            changed = true;
        }
        shadows.SanitizeCascades();

        int count = int(shadows.CascadeCount());
        m_SelectedCascade = std::clamp(m_SelectedCascade, 0, count - 1);

        ImGui::PushID("CascadeEditor");

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = std::max(ImGui::GetContentRegionAvail().x, 60.0f);
        const float height = ImGui::GetFrameHeight() * 1.5f;
        const float handleHalfWidth = 5.0f;

        auto boundary = [&](int i) { return i <= 0 ? 0.0f : (i >= count ? 1.0f : shadows.cascadeSplits[i - 1]); };
        auto toColor = [](const glm::vec3& c, float alpha = 1.0f) {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, alpha));
        };

        // The cascades : a click selects one
        for (int c = 0; c < count; c++)
        {
            const float x0 = origin.x + boundary(c) * width;
            const float x1 = origin.x + boundary(c + 1) * width;
            ImGui::SetCursorScreenPos(ImVec2(x0, origin.y));
            ImGui::PushID(c);
            // The handles are drawn over the cascades : without this the cascade under the cursor keeps the hover and a handle never gets it
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::InvisibleButton("##Cascade", ImVec2(std::max(x1 - x0, 1.0f), height)))
                m_SelectedCascade = c;
            ImGui::PopID();

            draw->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + height), toColor(shadows.cascadeColors[c]));

            char label[16];
            std::snprintf(label, sizeof(label), "%d", c + 1);
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            if (x1 - x0 > textSize.x + 4.0f)
            {
                // Dark or light, whichever reads on the color
                const glm::vec3& color = shadows.cascadeColors[c];
                const float luma = 0.299f * color.r + 0.587f * color.g + 0.114f * color.b;
                draw->AddText(ImVec2((x0 + x1 - textSize.x) * 0.5f, origin.y + (height - textSize.y) * 0.5f),
                              luma > 0.55f ? IM_COL32(0, 0, 0, 220) : IM_COL32(255, 255, 255, 230), label);
            }
        }

        // The selection is outlined
        draw->AddRect(ImVec2(origin.x + boundary(m_SelectedCascade) * width, origin.y),
                      ImVec2(origin.x + boundary(m_SelectedCascade + 1) * width, origin.y + height), IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);

        // The splits : a handle each, over the cascades
        int removeSplit = -1;
        for (int i = 0; i + 1 < count; i++)
        {
            const float x = origin.x + shadows.cascadeSplits[i] * width;
            ImGui::SetCursorScreenPos(ImVec2(x - handleHalfWidth, origin.y));
            ImGui::PushID(1000 + i);
            ImGui::InvisibleButton("##Split", ImVec2(handleHalfWidth * 2.0f, height));
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            ImGui::PopID();

            if (hovered || active)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

            if (active)
            {
                const float minGap = 0.001f;
                const float low = boundary(i) + minGap;
                const float high = boundary(i + 2) - minGap;
                const float value = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / width, low, std::max(low, high));
                if (value != shadows.cascadeSplits[i])
                {
                    shadows.cascadeSplits[i] = value;
                    changed = true;
                }
            }
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                removeSplit = i;
            if (hovered || active)
            {
                ImGui::BeginTooltip();
                ImGui::Text("Cascade %d ends at %.1f%% of the range", i + 1, shadows.cascadeSplits[i] * 100.0f);
                ImGui::EndTooltip();
            }

            const ImU32 handleColor = (hovered || active) ? IM_COL32(255, 220, 120, 255) : IM_COL32(255, 255, 255, 255);
            draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), IM_COL32(0, 0, 0, 255), 4.0f);
            draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), handleColor, 2.0f);
            draw->AddTriangleFilled(ImVec2(x - handleHalfWidth, origin.y + height), ImVec2(x + handleHalfWidth, origin.y + height),
                                    ImVec2(x, origin.y + height - handleHalfWidth * 1.6f), handleColor);
        }

        if (removeSplit >= 0)
        {
            shadows.cascadeSplits.erase(shadows.cascadeSplits.begin() + removeSplit);
            shadows.cascadeColors.erase(shadows.cascadeColors.begin() + removeSplit + 1);
            changed = true;
        }

        // Under the bar : the selected cascade, and the buttons that add or merge cascades
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height + ImGui::GetStyle().ItemSpacing.y));
        count = int(shadows.CascadeCount());
        m_SelectedCascade = std::clamp(m_SelectedCascade, 0, count - 1);

        auto selectedBoundary = [&](int i) { return i <= 0 ? 0.0f : (i >= count ? 1.0f : shadows.cascadeSplits[i - 1]); };
        if (ImGui::ColorEdit3("##CascadeColor", &shadows.cascadeColors[m_SelectedCascade].x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
            changed = true;
        if (ImGui::IsItemHovered())
            Tooltip("Color of the selected cascade in the \"Shadow Cascades\" debug view.");
        ImGui::SameLine();
        ImGui::Text("Cascade %d of %d : %.1f%% to %.1f%%", m_SelectedCascade + 1, count,
                    selectedBoundary(m_SelectedCascade) * 100.0f, selectedBoundary(m_SelectedCascade + 1) * 100.0f);

        ImGui::BeginDisabled(uint32_t(count) >= Projects::ShadowSettings::MaxCascades);
        if (ImGui::Button("Split"))
        {
            // The selected cascade becomes two halves
            const float middle = (selectedBoundary(m_SelectedCascade) + selectedBoundary(m_SelectedCascade + 1)) * 0.5f;
            shadows.cascadeSplits.insert(shadows.cascadeSplits.begin() + m_SelectedCascade, middle);
            shadows.cascadeColors.insert(shadows.cascadeColors.begin() + m_SelectedCascade + 1, shadows.cascadeColors[m_SelectedCascade]);
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            Tooltip("Cuts the selected cascade in two.");

        ImGui::SameLine();
        ImGui::BeginDisabled(count <= 1);
        if (ImGui::Button("Remove"))
        {
            // Its range goes to the next cascade (to the previous one for the last)
            const int splitIndex = m_SelectedCascade < count - 1 ? m_SelectedCascade : m_SelectedCascade - 1;
            shadows.cascadeSplits.erase(shadows.cascadeSplits.begin() + splitIndex);
            shadows.cascadeColors.erase(shadows.cascadeColors.begin() + m_SelectedCascade);
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            Tooltip("Removes the selected cascade : its range goes to its neighbour.");

        ImGui::PopID();

        if (changed)
            shadows.SanitizeCascades();
        return changed;
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

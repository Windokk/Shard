#include "physics_material_editor_panel.hpp"

#include "engine/world/engine.hpp"
#include "engine/assets/assetID.hpp"
#include "engine/assets/project/project.hpp"
#include "engine/assets/serialization/collision/collision_json.hpp"
#include "engine/core/diagnostics/logger.hpp"

#include "imgui/imgui.h"

#include "apps/editor/gui/popups.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cfloat>
#include <cstring>

namespace Shard::Editor::GUI{

    using namespace Shard::Engine;

    namespace {
        const char* kCombines[] = { "Average", "Min", "Max", "Multiply" };
    }

    void PhysicsMaterialEditorPanel::Open(const Filesystem::Path& newPath)
    {
        path = newPath;
        isOpen = true;
        focusRequested = true;
        Load();
    }

    void PhysicsMaterialEditorPanel::Load()
    {
        dirty = false;
        loaded = false;
        material = Collision::PhysicsMaterial();
        nameInProject = Core::GetEngine().GetFileManager()->GetFileInfos(path).nameInProject;

        if(!path.Exists()){
            DEBUG_ERROR("Physics material at path \"" + path.full + "\" doesn't exist");
            return;
        }

        nlohmann::json data = nlohmann::json::parse(path.ReadFile(), nullptr, false);
        if(data.is_discarded()){
            DEBUG_ERROR("Failed to parse physics material \"" + path.full + "\"");
            return;
        }

        std::vector<std::string> problems;
        material = Serialization::MaterialFromJson(nameInProject, data, &problems);
        for(const std::string& problem : problems)
            DEBUG_WARNING(problem);
        loaded = true;
    }

    bool PhysicsMaterialEditorPanel::Save()
    {
        if(!loaded)
            return false;

        if(!path.WriteFile(Serialization::MaterialToJson(material).dump(4) + "\n")){
            DEBUG_ERROR("Failed to save physics material: " + path.full);
            return false;
        }

        Projects::ReloadPhysicsMaterials(*Core::GetEngine().GetAssetIDManager());
        dirty = false;
        return true;
    }

    void PhysicsMaterialEditorPanel::Draw()
    {
        if(!isOpen)
            return;

        ImGui::SetNextWindowSize(ImVec2(420, 400), ImGuiCond_FirstUseEver);
        if(focusRequested){
            ImGui::SetNextWindowFocus();
            focusRequested = false;
        }

        const std::string title = "Physics Material Editor - " + path.GetFilename() + "###PhysicsMaterialEditor";
        const bool wasOpen = isOpen;

        if(ImGui::Begin(title.c_str(), &isOpen)){
            if(!loaded){
                ImGui::TextDisabled("This file could not be read.");
                if(ImGui::Button("Reload"))
                    Load();
            }
            else{
                struct Preset { const char* name; float friction; float restitution; float density; };
                static const Preset kPresets[] = {
                    { "Ice", 0.03f, 0.05f, 917.0f },
                    { "Rubber", 1.0f, 0.8f, 1100.0f },
                    { "Wood", 0.5f, 0.2f, 700.0f },
                    { "Concrete", 0.7f, 0.05f, 2400.0f },
                    { "Steel", 0.6f, 0.15f, 7800.0f },
                    { "Bouncy ball", 0.6f, 0.9f, 1100.0f }
                };

                auto section = [](const char* label){
                    ImGui::Spacing();
                    ImGui::SeparatorText(label);
                };

                auto row = [](const char* label, const char* tooltip){
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(label);
                    if(tooltip && ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", tooltip);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                };

                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("Preset");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
                if(ImGui::BeginCombo("##Preset", "Fill from...")){
                    for(const Preset& preset : kPresets){
                        if(ImGui::Selectable(preset.name)){
                            material.friction = preset.friction;
                            material.restitution = preset.restitution;
                            material.density = preset.density;
                            dirty = true;
                        }
                    }
                    ImGui::EndCombo();
                }

                const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp;

                section("Contact");
                if(ImGui::BeginTable("##Contact", 2, flags)){
                    ImGui::TableSetupColumn("##Label", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
                    ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch);

                    row("Friction", "How much the surface resists sliding. 0 is ice, 1 is rubber on concrete.");
                    dirty |= ImGui::SliderFloat("##Friction", &material.friction, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

                    row("Restitution", "Bounciness. 0 stops dead, 1 keeps all the speed.");
                    dirty |= ImGui::SliderFloat("##Restitution", &material.restitution, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

                    row("Combine", "How friction and restitution are merged with the material of the other body.");
                    int combine = int(material.combine);
                    if(ImGui::Combo("##Combine", &combine, kCombines, IM_ARRAYSIZE(kCombines))){
                        material.combine = Collision::Combine(combine);
                        dirty = true;
                    }
                    ImGui::EndTable();
                }

                section("Mass");
                if(ImGui::BeginTable("##Mass", 2, flags)){
                    ImGui::TableSetupColumn("##Label", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
                    ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch);

                    row("Density", "Mass per volume, for a body whose mass is not set by hand.");
                    dirty |= ImGui::DragFloat("##Density", &material.density, 10.0f, 1.0f, 50000.0f, "%.0f kg/m3");
                    ImGui::EndTable();
                }

                section("Surface");
                if(ImGui::BeginTable("##Surface", 2, flags)){
                    ImGui::TableSetupColumn("##Label", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
                    ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch);

                    row("Tag", "Reported on collision events, for footsteps, impact sounds and effects.");
                    char surface[128];
                    std::strncpy(surface, material.surface.c_str(), sizeof(surface) - 1);
                    surface[sizeof(surface) - 1] = '\0';
                    if(ImGui::InputTextWithHint("##SurfaceTag", "Surface.Metal.Steel", surface, sizeof(surface))){
                        material.surface = surface;
                        dirty = true;
                    }
                    ImGui::EndTable();
                }

                section("Used by profiles");
                bool any = false;
                for(const Collision::Profile* profile : Collision::ProfileRegistry::Get().All()){
                    if(profile->material != nameInProject)
                        continue;
                    ImGui::BulletText("%s", profile->name.c_str());
                    any = true;
                }
                if(!any)
                    ImGui::TextDisabled("No profile uses this material yet.");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::BeginDisabled(!dirty);
                if(ImGui::Button("Save"))
                    Save();
                ImGui::EndDisabled();

                ImGui::SameLine();
                ImGui::BeginDisabled(!dirty);
                if(ImGui::Button("Revert"))
                    Load();
                ImGui::EndDisabled();

                if(dirty){
                    ImGui::SameLine();
                    ImGui::TextDisabled("(unsaved changes)");
                }
            }
        }
        ImGui::End();

        if(wasOpen && !isOpen && dirty){
            isOpen = true;

            Popups::ConfirmUnsavedChanges(path.GetFilename(),
                [this](){ Save(); isOpen = false; },
                [this](){ dirty = false; isOpen = false; });
        }
    }
}

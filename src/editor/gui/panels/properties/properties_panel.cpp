#include "properties_panel.hpp"

#include "engine/core/reflection_fields.hpp"
#include "engine/core/engine.hpp"
#include "engine/levels/level.hpp"
#include "engine/levels/level_manager.hpp"

#include "editor/gui/main_window.hpp"
#include "editor/gui/dragdrop/asset_drag_drop.hpp"
#include "editor/gui/IconsLucide.h"

#include <glm/glm.hpp>
#include <cmath>
#include <type_traits>
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <string>

#include "engine/objects/components/audio/audio_source.hpp"
#include "engine/objects/components/misc/transform.hpp"
#include "engine/objects/components/rendering/camera.hpp"
#include "engine/objects/components/rendering/model_component.hpp"
#include "engine/objects/components/rendering/light_component.hpp"
#include "engine/objects/components/rendering/probe_volume.hpp"
#include "engine/objects/components/physics/physics_body.hpp"
#include "engine/objects/components/core/registry/component_registry.hpp"
#include "engine/rendering/renderer/renderer.hpp"
#include "engine/rendering/lighting/probe_manager.hpp"

using namespace Shard::Engine::Objects;
using namespace Shard::Engine::Objects::Components;

namespace Shard::Editor::GUI{

    static void MarkLevelDirty()
    {
        if (auto* level = Engine::Core::GetEngine().GetLevelManager()->GetLevelAt(0))
            level->SetDirty(true);
    }

    template<typename T>
    bool InputVector4(const char* label, T v[4], float speed = 0.1f, float min = 0.0f, float max = 0.0f)
    {
        bool changed = false;

        ImGui::PushID(label);

        float lineHeight = ImGui::GetFrameHeight();
        ImVec2 fieldSize = ImVec2(ImGui::GetContentRegionAvail().x / 4.0f - 4.0f, 0);

        const ImU32 colors[4] = {
            IM_COL32(220, 50, 50, 255),   // X - Red
            IM_COL32(50, 200, 70, 255),   // Y - Green
            IM_COL32(80, 120, 255, 255),  // Z - Blue
            IM_COL32(220, 180, 60, 255)   // W - Purple
        };

        for (int i = 0; i < 4; i++)
        {
            ImGui::PushID(i);

            ImVec2 cursorPos = ImGui::GetCursorScreenPos();

            // Draw colored left bar
            ImGui::GetWindowDrawList()->AddRectFilled(
                cursorPos,
                ImVec2(cursorPos.x + 3.0f, cursorPos.y + lineHeight),
                colors[i]
            );

            // Offset input so it doesn't overlap bar
            ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + 4.0f, cursorPos.y));

            ImGui::PushItemWidth(fieldSize.x - 4.0f);

            if constexpr (std::is_same_v<T, int>) {
                changed |= ImGui::DragInt("##v", &v[i], speed, (int)min, (int)max);
            } else if constexpr (std::is_same_v<T, unsigned int>) {
                int temp = static_cast<int>(v[i]);
                if (ImGui::DragInt("##v", &temp, speed, 0, (int)max)) {
                    v[i] = static_cast<unsigned int>(temp);
                    changed = true;
                }
            } else if constexpr (std::is_floating_point_v<T>) {
                changed |= ImGui::DragFloat("##v", &v[i], speed, min, max);
            }

            ImGui::PopItemWidth();

            if (i < 3)
                ImGui::SameLine();

            ImGui::PopID();
        }

        ImGui::PopID();

        return changed;
    }

    template<typename T>
    bool InputVector3(const char* label, T v[3], float speed = 0.1f, float min = 0.0f, float max = 0.0f)
    {
        bool changed = false;

        ImGui::PushID(label);

        float lineHeight = ImGui::GetFrameHeight();
        ImVec2 fieldSize = ImVec2(ImGui::GetContentRegionAvail().x / 3.0f - 4.0f, 0);

        const ImU32 colors[3] = {
            IM_COL32(220, 50, 50, 255),   // X - Red
            IM_COL32(50, 200, 70, 255),   // Y - Green
            IM_COL32(80, 120, 255, 255)   // Z - Blue
        };

        for (int i = 0; i < 3; i++)
        {
            ImGui::PushID(i);

            ImVec2 cursorPos = ImGui::GetCursorScreenPos();

            // Draw colored left bar
            ImGui::GetWindowDrawList()->AddRectFilled(
                cursorPos,
                ImVec2(cursorPos.x + 3.0f, cursorPos.y + lineHeight),
                colors[i]
            );

            // Offset input so it doesn't overlap bar
            ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + 4.0f, cursorPos.y));

            ImGui::PushItemWidth(fieldSize.x - 4.0f);
            
            if constexpr (std::is_same_v<T, int>) {
                changed |= ImGui::DragInt("##v", &v[i], speed, (int)min, (int)max);
            } else if constexpr (std::is_same_v<T, unsigned int>) {
                int temp = static_cast<int>(v[i]);
                if (ImGui::DragInt("##v", &temp, speed, 0, (int)max)) {
                    v[i] = static_cast<unsigned int>(temp);
                    changed = true;
                }
            } else if constexpr (std::is_floating_point_v<T>) {
                changed |= ImGui::DragFloat("##v", &v[i], speed, min, max);
            }
            
            ImGui::PopItemWidth();

            if (i < 2)
                ImGui::SameLine();

            ImGui::PopID();
        }

        ImGui::PopID();

        return changed;
    }

    template<typename T>
    bool InputVector2(const char* label, T v[2], float speed = 0.1f, float min = -FLT_MAX, float max = FLT_MAX)
    {
        bool changed = false;
        ImGui::PushID(label);

        float lineHeight = ImGui::GetFrameHeight();
        ImVec2 fieldSize = ImVec2(ImGui::GetContentRegionAvail().x / 2.0f - 4.0f, 0);

        const ImU32 colors[2] = {
            IM_COL32(220, 50, 50, 255),   // X - Red
            IM_COL32(50, 200, 70, 255),   // Y - Green
        };

        for (int i = 0; i < 2; i++)
        {
            ImGui::PushID(i);
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();

            // Draw colored left bar
            ImGui::GetWindowDrawList()->AddRectFilled(
                cursorPos,
                ImVec2(cursorPos.x + 3.0f, cursorPos.y + lineHeight),
                colors[i]
            );

            // Offset input so it doesn't overlap bar
            ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + 4.0f, cursorPos.y));
            ImGui::PushItemWidth(fieldSize.x - 4.0f);

            if constexpr (std::is_same_v<T, int>) {
                changed |= ImGui::DragInt("##v", &v[i], speed, (int)min, (int)max);
            } else if constexpr (std::is_same_v<T, unsigned int>) {
                int temp = static_cast<int>(v[i]);
                if (ImGui::DragInt("##v", &temp, speed, 0, (int)max)) {
                    v[i] = static_cast<unsigned int>(temp);
                    changed = true;
                }
            } else if constexpr (std::is_floating_point_v<T>) {
                changed |= ImGui::DragFloat("##v", &v[i], speed, min, max);
            }

            ImGui::PopItemWidth();
            if (i < 1) ImGui::SameLine();
            ImGui::PopID();
        }

        ImGui::PopID();
        return changed;
    }

    template<typename MatType>
    bool InputMatrix(const char* label, MatType& m, float speed = 0.1f)
    {
        constexpr int C = MatType::length();                     // columns
        constexpr int R = MatType::col_type::length();            // rows

        static const ImU32 axisColors[4] =
        {
            IM_COL32(220, 50, 50, 255),   // X
            IM_COL32(80, 200, 80, 255),   // Y
            IM_COL32(80, 120, 220, 255),  // Z
            IM_COL32(220, 180, 60, 255)   // W
        };

        bool changed = false;

        ImGui::PushID(label);

        float cellWidth = ImGui::GetContentRegionAvail().x / C;
        float lineHeight = ImGui::GetFrameHeight();

        ImVec2 pos = ImGui::GetCursorScreenPos();

        for (int r = 0; r < R; r++)
        {
            for (int c = 0; c < C; c++)
            {
                ImGui::PushID(r * C + c);

                ImGui::SetCursorScreenPos(ImVec2(pos.x + 8.0f + cellWidth * c, pos.y + 4 + lineHeight * r));

                // Colored axis bar
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImGui::GetCursorScreenPos(),
                    ImVec2(ImGui::GetCursorScreenPos().x - 3.0f, ImGui::GetCursorScreenPos().y + lineHeight),
                    axisColors[r]
                );

                ImGui::PushItemWidth(cellWidth - 6.0f);

                float v = m[c][r];

                if (ImGui::DragFloat("##cell", &v, speed))
                {
                    m[c][r] = v;
                    changed = true;
                }

                ImGui::PopItemWidth();

                if (c < C - 1)
                    ImGui::SameLine();

                ImGui::PopID();
            }
        }

        ImGui::PopID();

        return changed;
    }

    bool DrawQuatEuler(const char* label, glm::quat& rot, float min = 0.0f, float max = 0.0f)
    {
        static std::unordered_map<void*, glm::vec3> cachedEuler;
        glm::vec3& vec = cachedEuler[&rot];

        glm::quat cachedQuat = glm::quat(glm::radians(vec));
        if (glm::abs(glm::dot(cachedQuat, rot)) < 0.9999f)
            vec = glm::degrees(glm::eulerAngles(rot));

        if (InputVector3<float>(label, &vec.x, 0.1f, min, max))
        {
            rot = glm::quat(glm::radians(vec));
            return true;
        }

        return false;
    }

    void DrawPhysicsBodyShapes(std::shared_ptr<Component> comp)
    {
        auto body = std::dynamic_pointer_cast<PhysicsBody>(comp);
        if (!body)
            return;

        ImGui::Separator();
        ImGui::Text("Shapes");

        static const char* shapeTypeNames[] = { "Sphere", "Box", "Capsule", "Cylinder" };

        size_t indexToRemove = static_cast<size_t>(-1);

        for (size_t i = 0; i < body->GetShapeCount(); i++)
        {
            ImGui::PushID((int)i);

            bool removable = body->GetShapeCount() > 1;
            float removeButtonWidth = ImGui::GetFrameHeight();
            float rightAlignX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - removeButtonWidth;

            if (removable)
                ImGui::SetNextItemAllowOverlap();

            std::string label = "Shape " + std::to_string(i);
            bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

            if (removable)
            {
                ImGui::SameLine();
                ImGui::SetCursorPosX(rightAlignX);

                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, removeButtonWidth * 0.5f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.27f, 0.27f, 0.45f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.27f, 0.27f, 0.70f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.45f, 0.45f, 1.0f));

                if (ImGui::Button((std::string(ICON_LC_X) + "##RemoveShape").c_str(), ImVec2(removeButtonWidth, removeButtonWidth)))
                    indexToRemove = i;

                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Remove Shape");
            }

            if (open)
            {
                bool paramsChanged = false;

                if (ImGui::BeginTable("##ShapeTable", 2, ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("##Property", ImGuiTableColumnFlags_WidthStretch, 90.0f);
                    ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch, 90.0f);

                    auto row = [](const char* label)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text(label);
                        ImGui::TableSetColumnIndex(1);
                    };

                    int currentType = static_cast<int>(body->GetShapeType(i));

                    row("Type");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::Combo("##ShapeType", &currentType, shapeTypeNames, IM_ARRAYSIZE(shapeTypeNames)))
                    {
                        body->SetShapeType(i, static_cast<Physics::PhysicsShape>(currentType));
                        MarkLevelDirty();
                    }

                    switch (body->GetShapeType(i))
                    {
                        case Physics::PhysicsShape::SPHERE:
                        {
                            auto& p = body->GetShapeParams<SphereParams>(i);
                            row("Radius");
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            paramsChanged |= ImGui::DragFloat("##Radius", &p.radius, 0.05f, 0.001f, FLT_MAX);
                            break;
                        }
                        case Physics::PhysicsShape::BOX:
                        {
                            auto& p = body->GetShapeParams<BoxParams>(i);
                            row("Half Extent");
                            paramsChanged |= InputVector3<float>("HalfExtent", &p.halfExtent.x, 0.05f);
                            break;
                        }
                        case Physics::PhysicsShape::CAPSULE:
                        {
                            auto& p = body->GetShapeParams<CapsuleParams>(i);
                            row("Radius");
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            paramsChanged |= ImGui::DragFloat("##Radius", &p.radius, 0.05f, 0.001f, FLT_MAX);
                            row("Half Height");
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            paramsChanged |= ImGui::DragFloat("##HalfHeight", &p.halfHeight, 0.05f, 0.001f, FLT_MAX);
                            break;
                        }
                        case Physics::PhysicsShape::CYLINDER:
                        {
                            auto& p = body->GetShapeParams<CylinderParams>(i);
                            row("Radius");
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            paramsChanged |= ImGui::DragFloat("##Radius", &p.radius, 0.05f, 0.001f, FLT_MAX);
                            row("Half Height");
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            paramsChanged |= ImGui::DragFloat("##HalfHeight", &p.halfHeight, 0.05f, 0.001f, FLT_MAX);
                            break;
                        }
                    }

                    row("Offset");
                    paramsChanged |= InputVector3<float>("Offset", &body->GetShapeOffset(i).x, 0.05f);

                    row("Rotation");
                    paramsChanged |= DrawQuatEuler("Rotation", body->GetShapeRotation(i));

                    ImGui::EndTable();
                }

                if (paramsChanged)
                {
                    body->MarkShapesDirty();
                    MarkLevelDirty();
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        if (indexToRemove != static_cast<size_t>(-1))
        {
            body->RemoveShape(indexToRemove);
            MarkLevelDirty();
        }

        if (ImGui::Button("Add Shape"))
        {
            body->AddShape();
            MarkLevelDirty();
        }
    }

    void DrawProbeVolumeBake(std::shared_ptr<Component> comp)
    {
        auto volume = std::dynamic_pointer_cast<ProbeVolume>(comp);
        if (!volume)
            return;

        auto probeManager = Engine::Core::GetEngine().GetRenderer()->GetProbeManager();
        if (!probeManager)
            return;

        ImGui::Separator();
        ImGui::Text("Baked GI");

        const bool baking = probeManager->IsBaking();
        const bool bakingThis = probeManager->GetBakingVolume() == volume.get();
        const bool baked = probeManager->IsVolumeBaked(volume.get());
        const bool hasFile = !volume->bakedData.empty();

        if (bakingThis)
        {
            ImGui::TextDisabled("Baking - %s (%d%%)", probeManager->GetBakePhase(), (int)std::lround(probeManager->GetBakeProgress() * 100.0f));
        }
        else if (baked)
        {
            ImGui::TextDisabled("Baked : %s", volume->bakedData.c_str());
            ImGui::TextDisabled("Static - re-bake to pick up lighting or geometry changes.");
        }
        else if (hasFile)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), "%s is out of date - running live.", volume->bakedData.c_str());
        }
        else
        {
            ImGui::TextDisabled("Live (not baked) - traced every frame.");
        }

        // Only an active volume in the loaded level can be baked (it needs its GPU grid), and only one
        // bake runs at a time.
        ImGui::BeginDisabled(baking || !volume->Active());
        if (ImGui::Button((std::string(baked || hasFile ? "Re-bake" : "Bake") + "##ProbeVolumeBake").c_str()))
            volume->Bake();
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Traces this volume to convergence and saves the result next to the level,\nso it loads instantly instead of being recomputed on every level load.");

        if (hasFile)
        {
            ImGui::SameLine();

            ImGui::BeginDisabled(baking);
            if (ImGui::Button("Delete Bake##ProbeVolumeBake"))
            {
                volume->ClearBake();
                MarkLevelDirty();
            }
            ImGui::EndDisabled();

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Deletes the baked file and puts this volume back on live, real-time updates.");
        }
    }

    void PropertiesPanel::Draw(std::shared_ptr<Actor> actor)
    {
        ImGui::Begin("Properties");

        if(actor){
            
            DrawActorInfo(actor);

            std::shared_ptr<Component> componentToRemove = nullptr;

            for (int i = 0; i < actor->GetComponents().size(); i++)
            {
                ImGui::PushID(i);
                if (DrawComponent(actor->GetComponents()[i]))
                    componentToRemove = actor->GetComponents()[i];
                ImGui::PopID();
            }

            if (componentToRemove)
            {
                actor->RemoveComponent(componentToRemove);
                MarkLevelDirty();
            }

            ImGui::Separator();

            ImGuiStyle& style = ImGui::GetStyle();

            float size = ImGui::CalcTextSize("Add Component...").x + style.FramePadding.x * 2.0f;
            float avail = ImGui::GetContentRegionAvail().x;

            float off = (avail - size) * .5f;
            if (off > 0.0f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);


            if (ImGui::Button("Add Component...")) {
                ImGui::OpenPopup("AddComponentPopup");
            }

            if (ImGui::BeginPopup("AddComponentPopup")) {
                DrawAddComponentMenu(actor);
                ImGui::EndPopup();
            }

            // Right-click anywhere in the panel that isn't already an item (a component header, the
            // remove/active buttons, ...) opens the same menu, same as the "Add Component..." button.
            if (ImGui::BeginPopupContextWindow("##PropertiesAddComponentContext",
                    ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                DrawAddComponentMenu(actor);
                ImGui::EndPopup();
            }
        }

        ImGui::End();
    }

    void PropertiesPanel::DrawAddComponentMenu(std::shared_ptr<Actor> actor)
    {
        if (ImGui::MenuItem("Light")) {
            actor->AddComponent<Engine::Objects::Components::Light>();
            MarkLevelDirty();
        }

        if (ImGui::MenuItem("Camera")) {
            actor->AddComponent<Engine::Objects::Components::Camera>();
            MarkLevelDirty();
        }

        if (ImGui::MenuItem("Audio Source")) {
            actor->AddComponent<Engine::Objects::Components::AudioSource>();
            MarkLevelDirty();
        }

        if (ImGui::MenuItem("Physics Body")) {
            actor->AddComponent<Engine::Objects::Components::PhysicsBody>();
            MarkLevelDirty();
        }

        if (ImGui::MenuItem("Model")) {
            actor->AddComponent<Engine::Objects::Components::Model>();
            MarkLevelDirty();
        }

        if (ImGui::MenuItem("Probe Volume")) {
            actor->AddComponent<Engine::Objects::Components::ProbeVolume>();
            MarkLevelDirty();
        }

        // Everything registered through REGISTER_COMPONENT (game-side custom components, e.g.
        // scripts) - these aren't special-cased engine types like the ones above, so the registry is
        // the only way to instantiate them by name (Level::DeserializeComponents does the same thing
        // for level files). CreateComponentByName hands back a parent-less component (see
        // DECLARE_COMPONENT); AddComponentRaw is what wires it into this actor/level.
        const auto& customComponents = GetComponentRegistry().GetAll();
        if (!customComponents.empty())
        {
            ImGui::Separator();

            // Sorted so the menu has a stable order instead of reshuffling with the registry's
            // internal (unordered_map) iteration order.
            std::vector<std::string> names;
            names.reserve(customComponents.size());
            for (auto& [name, factory] : customComponents)
                names.push_back(name);
            std::sort(names.begin(), names.end());

            for (const std::string& name : names)
            {
                if (ImGui::MenuItem(name.c_str()))
                {
                    std::shared_ptr<Component> component = GetComponentRegistry().CreateComponentByName(name);
                    if (component)
                    {
                        actor->AddComponentRaw(component);
                        MarkLevelDirty();
                    }
                }
            }
        }
    }

    void PropertiesPanel::DrawActorInfo(std::shared_ptr<Actor> actor)
    {
        char buffer[256];
        strcpy(buffer, actor->GetName().c_str());

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Name");
        ImGui::SameLine();

        if (ImGui::InputText("##ActorName", buffer, sizeof(buffer)))
        {
            actor->SetName(buffer);
            MarkLevelDirty();
        }

        ImGui::Text("Object ID: %d", actor->GetID().GetAsInt());
        ImGui::Text("Components: %zu", actor->GetComponents().size());
    }

    bool PropertiesPanel::DrawComponent(std::shared_ptr<Component> comp)
    {
        const ClassDescriptor* desc = comp->GetDescriptor();
        bool removeRequested = false;

        // The Transform is mandatory on every actor and cannot be removed.
        bool removable = !comp->IsInstanceOf<Transform>();

        bool active = comp->Active();
        std::string id = "##" + desc->name + "Active";

        ImGui::AlignTextToFramePadding();
        if (ImGui::Checkbox(id.c_str(), &active))
        {
            active ? comp->Activate() : comp->DeActivate();
            MarkLevelDirty();
        }

        ImGui::SameLine();

        float removeButtonWidth = ImGui::GetFrameHeight();
        float rightAlignX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - removeButtonWidth;

        if (removable)
            ImGui::SetNextItemAllowOverlap();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
        bool open = ImGui::CollapsingHeader(desc->name.c_str(), flags);

        if (removable)
        {
            ImGui::SameLine();
            ImGui::SetCursorPosX(rightAlignX);

            std::string removeId = std::string(ICON_LC_X) + "##" + desc->name + "Remove";

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, removeButtonWidth * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.27f, 0.27f, 0.45f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.27f, 0.27f, 0.70f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.45f, 0.45f, 1.0f));

            if (ImGui::Button(removeId.c_str(), ImVec2(removeButtonWidth, removeButtonWidth)))
            {
                removeRequested = true;
            }

            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Remove Component");
        }

        if (open)
        {
            if (ImGui::BeginTable("##PropertiesTable", 2, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("##Property", ImGuiTableColumnFlags_WidthStretch, 90.0f);
                ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch, 90.0f);

                for (FieldInfo* field : desc->fields)
                {
                    if (field->type == TypeID::Struct)
                    {
                        uint8_t* base = reinterpret_cast<uint8_t*>(comp.get()) + field->offset;
                        InstancedStruct* structPtr = reinterpret_cast<InstancedStruct*>(base);

                        for (auto& subField : structPtr->descriptor->fields)
                        {
                            void* valuePtr = FieldRead(*subField, structPtr->data);
                            DrawField(subField, valuePtr, comp);
                        }
                    }
                    else
                    {
                        void* valuePtr = FieldRead(*field, comp.get());
                        DrawField(field, valuePtr, comp);
                    }
                }
                
                ImGui::EndTable();
            }

            DrawPhysicsBodyShapes(comp);
            DrawProbeVolumeBake(comp);
        }

        return removeRequested;
    }

    void PropertiesPanel::DrawField(const FieldInfo *field, void *value, std::shared_ptr<Engine::Objects::Components::Component> comp, const Container *container, const int valueIndexInContainer)
    {
        const char * fieldName = field->name;

        bool readOnly = (field->flags & ReadOnly) && !(field->flags & Editable);

        if(valueIndexInContainer != -1)
            fieldName = std::to_string(valueIndexInContainer).c_str();

        ImGui::TableNextRow();

        // Column 0 : Label
        ImGui::TableSetColumnIndex(0);

        ImGui::AlignTextToFramePadding();
        ImGui::Text(fieldName);

        // Column 1 : Widget
        ImGui::TableSetColumnIndex(1);

        std::string id = "##" + std::string(fieldName);

        if(readOnly)
            ImGui::BeginDisabled();

        switch(container ? container->elementType : field->type){
            case TypeID::Float:
            {
                float* v = static_cast<float*>(value);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat(id.c_str(), v, 0.05f, field->min, field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::Double:
            {
                double* v = static_cast<double*>(value);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragScalar(id.c_str(), ImGuiDataType_Double, v, 0.05f, &field->min, &field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::Int8:
            {
                int8_t* v = static_cast<int8_t*>(value);
                int tmp = static_cast<int>(*v);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragInt(id.c_str(), &tmp, 0.05f, (int8_t)field->min, (int8_t)field->max))
                {
                    *v = static_cast<int8_t>(tmp);
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::Int16:
            {
                int16_t* v = static_cast<int16_t*>(value);
                int tmp = static_cast<int>(*v);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragInt(id.c_str(), &tmp, 1.0f, (int16_t)field->min, (int16_t)field->max))
                {
                    *v = static_cast<int16_t>(tmp);
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::Int32:
            {
                int* v = static_cast<int*>(value);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragInt(id.c_str(), v, 0.05f, (int32_t)field->min, (int32_t)field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::Int64:
            {
                int64_t* v = static_cast<int64_t*>(value);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragScalar(id.c_str(), ImGuiDataType_S64, v, 0.05f, (int64_t*)&field->min, (int64_t*)&field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::UInt8:
            {
                uint8_t* v = static_cast<uint8_t*>(value);
                int tmp = static_cast<int>(*v);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragInt(id.c_str(), &tmp, 1.0f, 0, (uint8_t)field->max))
                {
                    *v = static_cast<uint8_t>(tmp);
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::UInt16:
            {
                uint16_t* v = static_cast<uint16_t*>(value);
                int tmp = static_cast<int>(*v);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragInt(id.c_str(), &tmp, 1.0f, 0, (uint16_t)field->max))
                {
                    *v = static_cast<uint16_t>(tmp);
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::UInt32:
            {
                uint32_t* v = static_cast<uint32_t*>(value);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragScalar(id.c_str(), ImGuiDataType_U32, v, 0, (uint32_t*)&field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::UInt64:
            {
                uint64_t* v = static_cast<uint64_t*>(value);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragScalar(id.c_str(), ImGuiDataType_U64, v, 0, (uint64_t*)&field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::Bool:
            {
                bool* v = static_cast<bool*>(value);
                if (ImGui::Checkbox(id.c_str(), v))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::Asset:
            {
                Filesystem::AssetID* asset = static_cast<Filesystem::AssetID*>(value);

                static char buffer[256] = "";

                auto* manager = Engine::Core::GetEngine().GetAssetIDManager();

                if (asset)
                {
                    if (manager)
                    {
                        auto assetPtr = manager->GetAssetFromID(*asset);
                        if (assetPtr)
                        {
                            const std::string& name = assetPtr->baseInfos.nameInProject;

                            if (!name.empty())
                            {
                                strncpy(buffer, name.c_str(), sizeof(buffer) - 1);
                                buffer[sizeof(buffer) - 1] = '\0'; // enforce null-termination
                            }
                        }
                    }
                }

                if (asset)
                {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputText(id.c_str(), buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        if(manager)
                        {
                            *static_cast<Filesystem::AssetID*>(value) = manager->GetIDFromNameInProject(buffer);
                        }
                        FieldChangedEvent evt{ field };
                        comp->OnFieldChanged(evt); MarkLevelDirty();
                    }

                    // Accept an asset dragged from the browser onto this field - works for any
                    // AssetID field (mesh, materials, ...) since it only needs the dropped asset's
                    // nameInProject, which GetIDFromNameInProject resolves the same way as the text box.
                    if (ImGui::BeginDragDropTarget())
                    {
                        std::vector<std::string> dropped = DragDrop::AcceptAssetDragDropPayload();
                        if (manager && !dropped.empty())
                        {
                            *static_cast<Filesystem::AssetID*>(value) = manager->GetIDFromNameInProject(dropped[0]);
                            FieldChangedEvent evt{ field };
                            comp->OnFieldChanged(evt); MarkLevelDirty();
                        }
                        ImGui::EndDragDropTarget();
                    }
                }
                break;
            }
            case TypeID::String:
            {
                std::string* str = static_cast<std::string*>(value);
                static char buffer[256] = "";

                if (!str->empty()) {
                    strncpy(buffer, str->c_str(), sizeof(buffer) - 1);
                    buffer[sizeof(buffer) - 1] = '\0';
                }

                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputText(id.c_str(), buffer, sizeof(buffer)))
                {
                    *str = std::string(buffer);
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::Quat:
            {
                glm::quat* quat = static_cast<glm::quat*>(value);

                if (DrawQuatEuler(fieldName, *quat, field->min, field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::Enum:
            {
                int current;
                memcpy(&current, value, field->enumDesc->size);

                std::vector<const char*> items;
                for (auto& e : field->enumDesc->values)
                    items.push_back(e.name);

                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo(id.c_str(), &current,
                                items.data(), items.size()))
                {
                    memcpy(value, &current, field->enumDesc->size);

                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::Vector:
            {
                if (!field->container)
                    break;

                if (ImGui::TreeNode(id.c_str()))
                {
                    size_t count = field->container->Size(value);

                    for (size_t i = 0; i < count; i++)
                    {
                        void* element = field->container->GetByIndex(value, i);

                        ImGui::PushID((int)i);
                        DrawField(field, element, comp, field->container, i);
                        ImGui::PopID();
                    }

                    ImGui::TreePop();
                }
                break;
            }
            case TypeID::Vec4:
            {
                glm::vec4* vec = static_cast<glm::vec4*>(value);

                if (InputVector4<float>(fieldName, &vec->x, 0.1f, field->min, field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            } 
            case TypeID::Vec3:
            {
                glm::vec3* vec = static_cast<glm::vec3*>(value);

                if (InputVector3<float>(fieldName, &vec->x, 0.1f, field->min, field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::Vec2:
            {
                glm::vec2* vec = static_cast<glm::vec2*>(value);

                if (InputVector2<float>(fieldName, &vec->x, 0.1f, field->min, field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::IVec2:
            {
                glm::ivec2* vec = static_cast<glm::ivec2*>(value);

                if (InputVector2<int>(fieldName, &vec->x, 1.0f, (int)field->min, (int)field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            } 
            case TypeID::IVec3:
            {
                glm::ivec3* vec = static_cast<glm::ivec3*>(value);

                if (InputVector3<int>(fieldName, &vec->x, 1.0f, (int)field->min, (int)field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::IVec4:
            {
                glm::ivec2* vec = static_cast<glm::ivec2*>(value);

                if (InputVector4<int>(fieldName, &vec->x, 1.0f, (int)field->min, (int)field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::UVec2:
            {
                glm::uvec2* vec = static_cast<glm::uvec2*>(value);

                if (InputVector2<unsigned int>(fieldName, &vec->x, 1.0f, 0.0f, (int)field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            } 
            case TypeID::UVec3:
            {
                glm::uvec3* vec = static_cast<glm::uvec3*>(value);

                if (InputVector3<unsigned int>(fieldName, &vec->x, 1.0f, 0.0f, (int)field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::UVec4:
            {
                glm::uvec2* vec = static_cast<glm::uvec2*>(value);

                if (InputVector4<unsigned int>(fieldName, &vec->x, 1.0f, 0.0f, (int)field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::Mat2:
            {
                glm::mat2* mat = static_cast<glm::mat2*>(value);

                if (InputMatrix(field->name, *mat))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::Mat3:
            {
                glm::mat3* mat = static_cast<glm::mat3*>(value);

                if (InputMatrix(field->name, *mat))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::Mat4:
            {
                glm::mat4* mat = static_cast<glm::mat4*>(value);

                if (InputMatrix(field->name, *mat))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::ColorRGB:
            {
                COL_RGB* v = static_cast<COL_RGB*>(value);
                float value[3] = {v->r(), v->g(), v->b()};
                ImGui::SetNextItemWidth(-FLT_MIN);
                if(ImGui::ColorEdit3(id.c_str(), value)){

                    *v = COL_RGB(value[0], value[1], value[2]);

                    FieldChangedEvent evt{field};
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::ColorRGBA:
            {
                COL_RGBA* v = static_cast<COL_RGBA*>(value);
                float value[4] = {v->r(), v->g(), v->b(), v->a()};
                ImGui::SetNextItemWidth(-FLT_MIN);
                if(ImGui::ColorEdit4(id.c_str(), value)){

                    *v = COL_RGBA(value[0], value[1], value[2], value[3]);

                    FieldChangedEvent evt{field};
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }
                break;
            }
            case TypeID::CString:
            {
                char* str = static_cast<char*>(value);

                static char buffer[256];

                strncpy(buffer, str, sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = '\0';

                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputText(id.c_str(), buffer, sizeof(buffer)))
                {
                    strncpy(str, buffer, 255);
                    str[255] = '\0';

                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkLevelDirty();
                }

                break;
            }
        }

        if(readOnly)
            ImGui::EndDisabled();
    }
}
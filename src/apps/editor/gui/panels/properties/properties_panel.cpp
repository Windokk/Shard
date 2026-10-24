#include "properties_panel.hpp"
#include "apps/editor/gui/plugin_ui.hpp"

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"

#include "apps/editor/gui/main_window.hpp"
#include "apps/editor/gui/dragdrop/asset_drag_drop.hpp"
#include "apps/editor/gui/IconsLucide.h"
#include "apps/editor/gui/tag_widgets.hpp"
#include "apps/editor/commands/command_stack.hpp"

#include <glm/glm.hpp>
#include <cmath>
#include <type_traits>
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <string>

#include "engine/audio/audio_source.hpp"
#include "engine/audio/audio_listener.hpp"
#include "engine/audio/reverb_zone.hpp"
#include "engine/world/components/transform.hpp"
#include "engine/renderer/components/camera.hpp"
#include "engine/renderer/components/model_component.hpp"
#include "engine/renderer/components/light_component.hpp"
#include "engine/renderer/components/probe_volume.hpp"
#include "engine/renderer/components/reflection_probe.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/physics/gravity_field.hpp"
#include "engine/world/components/registry/component_registry.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/features/lighting/probe_manager.hpp"

using namespace Shard::Engine::Objects;
using namespace Shard::Engine::Objects::Components;

namespace Shard::Editor::GUI{

    static void MarkWorldDirty()
    {
        if (auto* world = Engine::Core::GetEngine().GetWorldManager()->GetWorldAt(0))
            world->SetDirty(true);
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
                        MarkWorldDirty();
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
                    MarkWorldDirty();
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        if (indexToRemove != static_cast<size_t>(-1))
        {
            body->RemoveShape(indexToRemove);
            MarkWorldDirty();
        }

        if (ImGui::Button("Add Shape"))
        {
            body->AddShape();
            MarkWorldDirty();
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
        const bool bakingThis = probeManager->GetBakingKey() == volume->GetRenderKey();
        const bool baked = probeManager->IsVolumeBaked(volume->GetRenderKey());
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

        // Only an active volume in the loaded world can be baked (it needs its GPU grid), and only one
        // bake runs at a time.
        ImGui::BeginDisabled(baking || !volume->Active());
        if (ImGui::Button((std::string(baked || hasFile ? "Re-bake" : "Bake") + "##ProbeVolumeBake").c_str()))
            volume->Bake();
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Traces this volume to convergence and saves the result next to the world,\nso it loads instantly instead of being recomputed on every world load.");

        if (hasFile)
        {
            ImGui::SameLine();

            ImGui::BeginDisabled(baking);
            if (ImGui::Button("Delete Bake##ProbeVolumeBake"))
            {
                volume->ClearBake();
                MarkWorldDirty();
            }
            ImGui::EndDisabled();

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Deletes the baked file and puts this volume back on live, real-time updates.");
        }
    }

    void DrawReflectionProbeBake(std::shared_ptr<Component> comp)
    {
        auto probe = std::dynamic_pointer_cast<ReflectionProbe>(comp);
        if (!probe)
            return;

        ImGui::Separator();
        ImGui::Text("Baked reflection");

        const float progress = probe->GetBakeProgress();
        const bool bakingThis = progress >= 0.0f;
        const bool hasFile = !probe->bakedData.empty();

        if (bakingThis)
        {
            ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0.0f));
        }
        else if (hasFile)
        {
            ImGui::TextDisabled("Baked : %s", probe->bakedData.c_str());
            ImGui::TextDisabled("Static - re-bake to pick up lighting or geometry changes.");
        }
        else
        {
            ImGui::TextDisabled("Not baked - this probe reflects nothing yet.");
        }

        // Only an active probe in the loaded world can be baked, and only one bake runs at a time
        ImGui::BeginDisabled(bakingThis || !probe->Active());
        if (ImGui::Button((std::string(hasFile ? "Re-bake" : "Bake") + "##ReflectionProbeBake").c_str()))
            probe->Bake();
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Path traces the whole sphere around this actor and saves it in the project's reflection_probes folder.\nThe surfaces inside the box reflect that picture.");

        if (bakingThis)
        {
            ImGui::SameLine();
            if (ImGui::Button("Cancel##ReflectionProbeBake"))
                probe->CancelBake();
        }
        else if (hasFile)
        {
            ImGui::SameLine();
            if (ImGui::Button("Delete Bake##ReflectionProbeBake"))
            {
                probe->ClearBake();
                MarkWorldDirty();
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Deletes the baked file : the probe reflects nothing again.");
        }
    }

    void PropertiesPanel::Draw(std::shared_ptr<Actor> actor, int selectionCount)
    {
        ImGui::Begin("Properties");

        drawnActor = actor;

        if(actor){

            if(selectionCount > 1)
                ImGui::TextDisabled("%d actors selected - editing \"%s\" (the last one selected)", selectionCount, actor->GetName().c_str());

            DrawActorInfo(actor);

            std::shared_ptr<Component> componentToRemove = nullptr;

            for (int i = 0; i < static_cast<int>(actor->GetComponents().size()); i++)
            {
                ImGui::PushID(i);
                if (DrawComponent(actor->GetComponents()[i]))
                    componentToRemove = actor->GetComponents()[i];
                if (const ClassDescriptor* described = actor->GetComponents()[i]->GetDescriptor())
                    DrawPluginInspectors(described->name, actor);       // what plugins draw below this component
                ImGui::PopID();
            }
            DrawPluginInspectors("", actor);                           // ... and below all of them

            if (componentToRemove)
                Commands::Actions::EditActor(actor, "Remove Component", [&]{ actor->RemoveComponent(componentToRemove); });

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

        CommitPendingEdits();

        drawnActor = nullptr;

        ImGui::End();
    }

    namespace
    {
        /// The position of a root actor is shown (and typed) as an absolute position in the world, in double : the frame it is stored in
        /// is the engine's business. A child's position stays relative to its parent.
        bool IsAbsolutePosition(const FieldInfo* field, const std::shared_ptr<Component>& comp, const Container* container)
        {
            if (container || !field || std::string(field->name) != "position")
                return false;
            auto transform = std::dynamic_pointer_cast<Transform>(comp);
            return transform && transform->parent && !std::dynamic_pointer_cast<Engine::Objects::Actor>(transform->parent->GetParent());
        }

        /// Where a transform was and where it is : the frame and the numbers in it
        struct PlacementState
        {
            uint32_t frame = 0;
            glm::vec3 position = glm::vec3(0.0f);
        };

        class PlaceCommand final : public Commands::Command
        {
        public:
            PlaceCommand(const std::shared_ptr<Transform>& t, PlacementState from, PlacementState to) : transform(t), before(from), after(to) {}

            void Undo() override { Apply(before); }
            void Redo() override { Apply(after); }
            bool IsValid() const override { return before.frame != after.frame || before.position != after.position; }
            const std::string& GetName() const override { return name; }

        private:
            void Apply(const PlacementState& state)
            {
                if (auto t = transform.lock()) {
                    t->frame = state.frame;
                    t->SetPosition(state.position);
                }
            }

            std::weak_ptr<Transform> transform;
            PlacementState before, after;
            std::string name = "position";
        };

        /// Three double fields for the absolute position. Returns true when it changed.
        bool InputAbsolutePosition(const char* id, const std::shared_ptr<Transform>& transform)
        {
            static PlacementState beforeEdit;
            static bool editing = false;

            glm::dvec3 value = transform->GetWorldCoords().ToDVec3();
            bool changed = false;

            const ImU32 colors[3] = { IM_COL32(220, 50, 50, 255), IM_COL32(50, 200, 70, 255), IM_COL32(80, 120, 255, 255) };
            const float lineHeight = ImGui::GetFrameHeight();
            const float width = ImGui::GetContentRegionAvail().x / 3.0f - 4.0f;

            ImGui::PushID(id);
            for (int i = 0; i < 3; i++)
            {
                ImGui::PushID(i);
                const ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(cursor, ImVec2(cursor.x + 3.0f, cursor.y + lineHeight), colors[i]);
                ImGui::SetCursorScreenPos(ImVec2(cursor.x + 4.0f, cursor.y));
                ImGui::SetNextItemWidth(width - 4.0f);

                glm::dvec3 edited = value;
                // Scientific notation only for what would not fit otherwise (a planet far from the origin) : 10 reads "10.000", not "1.00e+01"
                const char* format = std::abs(value[i]) >= 1.0e7 ? "%.3e" : "%.3f";
                const bool dragged = ImGui::DragScalar("##v", ImGuiDataType_Double, &edited[i], 0.1f, nullptr, nullptr, format);
                if (ImGui::IsItemActivated() && !editing) {
                    beforeEdit = { transform->frame, transform->position };
                    editing = true;
                }
                if (dragged && std::isfinite(edited[i])) {
                    value = edited;
                    transform->PlaceAt(Engine::Core::WorldPosition::FromDVec3(value));
                    changed = true;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && editing) {
                    editing = false;
                    Commands::CommandStack::Get().Record(std::make_unique<PlaceCommand>(
                        transform, beforeEdit, PlacementState{ transform->frame, transform->position }));
                }
                else if (ImGui::IsItemDeactivated())
                    editing = false;

                if (i < 2)
                    ImGui::SameLine();
                ImGui::PopID();
            }
            ImGui::PopID();
            return changed;
        }
    }

    void PropertiesPanel::DrawRecordedField(const FieldInfo* field, std::shared_ptr<Component> comp, Commands::FieldOwnerLocator locate)
    {
        void* owner = locate ? locate(*comp) : static_cast<void*>(comp.get());
        if (!owner)
            return;

        void* valuePtr = FieldRead(*field, owner);

        // Its undo is a PlaceCommand, recorded by the widget (the frame is not a field)
        if (!Commands::CanRecordField(field) || IsAbsolutePosition(field, comp, nullptr))
        {
            DrawField(field, valuePtr, comp);
            return;
        }

        Commands::ValueBuffer old(field);
        old.Capture(valuePtr);

        DrawField(field, valuePtr, comp);

        if (old.EqualsValue(valuePtr))
            return;

        // Changed again by the same drag : the value from before the first change is the one to go back to
        for (const PendingFieldEdit& edit : pendingFieldEdits)
            if (edit.component == comp && edit.field == field)
                return;

        pendingFieldEdits.push_back(PendingFieldEdit{ comp, field, std::move(locate), std::move(old) });
    }

    void PropertiesPanel::CommitPendingEdits()
    {
        if (pendingFieldEdits.empty() && !pendingActorEdit)
            return;

        // Still being edited ; or a gizmo drag is being recorded, which these edits must not end up in
        auto& history = Commands::CommandStack::Get();
        if (ImGui::IsAnyItemActive() || history.IsActive())
            return;

        history.Begin("Edit Properties");

        for (PendingFieldEdit& edit : pendingFieldEdits)
            history.Add(std::make_unique<Commands::ModifyFieldCommand>(edit.component, edit.field, std::move(edit.before), std::move(edit.locate)));
        pendingFieldEdits.clear();

        if (pendingActorEdit)
            history.Add(std::move(pendingActorEdit));

        history.End();
    }

    void PropertiesPanel::DrawAddComponentMenu(std::shared_ptr<Actor> actor)
    {
        namespace C = Engine::Objects::Components;

        // One entry of the menu : adds a component of type C::Type to the actor (undoable)
        #define SHARD_ADD_ITEM(label, Type) \
            if (ImGui::MenuItem(label)) \
                Commands::Actions::EditActor(actor, "Add Component", [&]{ actor->AddComponent<C::Type>(); })

        if (ImGui::BeginMenu("Rendering")) {
            SHARD_ADD_ITEM("Model", Model);
            SHARD_ADD_ITEM("Light", Light);
            SHARD_ADD_ITEM("Camera", Camera);
            ImGui::Separator();
            SHARD_ADD_ITEM("Probe Volume", ProbeVolume);
            SHARD_ADD_ITEM("Reflection Probe", ReflectionProbe);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Physics")) {
            SHARD_ADD_ITEM("Physics Body", PhysicsBody);
            if (ImGui::BeginMenu("Gravity Field")) {
                SHARD_ADD_ITEM("Uniform", UniformGravityField);
                SHARD_ADD_ITEM("Point", PointGravityField);
                SHARD_ADD_ITEM("Radial (Planet)", RadialGravityField);
                SHARD_ADD_ITEM("Volume", GravityVolume);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Audio")) {
            SHARD_ADD_ITEM("Audio Source", AudioSource);
            SHARD_ADD_ITEM("Audio Listener", AudioListener);
            SHARD_ADD_ITEM("Reverb Zone", ReverbZone);
            ImGui::EndMenu();
        }

        #undef SHARD_ADD_ITEM

        // Everything registered through REGISTER_COMPONENT (game-side custom components, e.g.
        // scripts) - these aren't special-cased engine types like the ones above, so the registry is
        // the only way to instantiate them by name (World::DeserializeComponents does the same thing
        // for world files). CreateComponentByName hands back a parent-less component (see
        // DECLARE_COMPONENT); AddComponentRaw is what wires it into this actor/world.
        const auto& customComponents = GetComponentRegistry().GetAll();
        if (!customComponents.empty() && ImGui::BeginMenu("Scripts"))
        {
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
                        Commands::Actions::EditActor(actor, "Add Component", [&]{ actor->AddComponentRaw(component); });
                    }
                }
            }
            ImGui::EndMenu();
        }
    }

    void PropertiesPanel::DrawActorInfo(std::shared_ptr<Actor> actor)
    {
        char buffer[256];
        strcpy(buffer, actor->GetName().c_str());

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Name");
        ImGui::SameLine();

        const std::string nameBefore = actor->GetName();

        if (ImGui::InputText("##ActorName", buffer, sizeof(buffer)))
        {
            actor->SetName(buffer);
            MarkWorldDirty();
        }

        // One undo step for the whole edit of the box, not one per letter
        if (ImGui::IsItemActivated())
        {
            nameEditActor = actor;
            nameBeforeEdit = nameBefore;
        }

        if (ImGui::IsItemDeactivated() && nameEditActor)
        {
            Commands::CommandStack::Get().Record(std::make_unique<Commands::RenameActorCommand>(nameEditActor, nameBeforeEdit, nameEditActor->GetName()));
            nameEditActor = nullptr;
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Tags");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("What this actor is : found with World::FindActorsWithTag, tested by tag queries. The children do not inherit them.");
        ImGui::SameLine();
        Engine::Core::TagContainer tags = actor->GetTags();
        if (TagWidgets::TagContainerEditor("##ActorTags", tags))
        {
            Commands::Actions::EditActor(actor, "Edit Tags", [&]{ actor->GetTags() = tags; });
            MarkWorldDirty();
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
            Commands::Actions::SetComponentActive(comp, active);

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

                // The component's own tags, before its fields
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("tags");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("What this part of the actor is (a hitbox, an interaction...) : found with World::FindComponentsWithTag.");
                ImGui::TableSetColumnIndex(1);
                Engine::Core::TagContainer componentTags = comp->GetTags();
                if (TagWidgets::TagContainerEditor("##ComponentTags", componentTags))
                {
                    if (drawnActor)
                        Commands::Actions::EditActor(drawnActor, "Edit Component Tags", [&]{ comp->GetTags() = componentTags; });
                    else
                        comp->GetTags() = componentTags;
                    MarkWorldDirty();
                }

                for (FieldInfo* field : desc->fields)
                {
                    if (field->type == TypeID::Struct)
                    {
                        uint8_t* base = reinterpret_cast<uint8_t*>(comp.get()) + field->offset;
                        InstancedStruct* structPtr = reinterpret_cast<InstancedStruct*>(base);

                        // Read through the component each time : the undo history may apply the value to a rebuilt one
                        const uint32_t offset = field->offset;
                        Commands::FieldOwnerLocator locate = [offset](Component& owner) -> void* {
                            return reinterpret_cast<InstancedStruct*>(reinterpret_cast<uint8_t*>(&owner) + offset)->data;
                        };

                        for (auto& subField : structPtr->descriptor->fields)
                            DrawRecordedField(subField, comp, locate);
                    }
                    else
                    {
                        DrawRecordedField(field, comp);
                    }
                }
                
                ImGui::EndTable();
            }

            if (comp->IsInstanceOf<PhysicsBody>() && drawnActor)
            {
                // The shapes are not reflected fields : an edit of them is undone by rebuilding the actor
                ordered_json shapesBefore = comp->Serialize();
                DrawPhysicsBodyShapes(comp);

                if (!pendingActorEdit && comp->Serialize() != shapesBefore)
                {
                    Commands::ActorState before = Commands::ActorState::Capture(drawnActor);

                    const auto& components = drawnActor->GetComponents();
                    const size_t index = std::find(components.begin(), components.end(), comp) - components.begin();
                    if (before.data["components"].is_array() && index < before.data["components"].size())
                        before.data["components"][index] = shapesBefore;

                    pendingActorEdit = std::make_unique<Commands::ActorSnapshotCommand>("Edit Physics Shapes", drawnActor, true);
                    pendingActorEdit->SetBefore(std::move(before));
                }
            }
            else
            {
                DrawPhysicsBodyShapes(comp);
            }

            DrawProbeVolumeBake(comp);
            DrawReflectionProbeBake(comp);
        }

        return removeRequested;
    }

    // The asset a field points at, Unreal-style : a button showing it (click = pick another from a list, drop = assign the
    // asset dragged from the browser), then "use the asset selected in the browser", "show it in the browser" and "clear".
    // A field already holding an asset only takes assets of the same type (a mesh field is not given a texture).
    static bool DrawAssetField(const std::string& id, Filesystem::AssetID& value, AssetBrowser* browser)
    {
        auto* manager = Engine::Core::GetEngine().GetAssetIDManager();
        if (!manager)
            return false;

        std::shared_ptr<Filesystem::AssetInfos> current = manager->GetAssetFromID(value);
        const std::string currentName = current ? current->baseInfos.nameInProject : std::string();

        // Resolves a nameInProject to an assignable asset, nullptr if it is not one this field accepts
        auto resolve = [&](const std::string& name) -> std::shared_ptr<Filesystem::AssetInfos>
        {
            if (name.empty())
                return nullptr;
            auto info = manager->GetAssetFromID(manager->GetIDFromNameInProject(name));
            if (!info || info->baseInfos.nameInProject != name || info->baseInfos.isDirectory)
                return nullptr;
            if (current && info->baseInfos.type != current->baseInfos.type)
                return nullptr;
            return info;
        };

        bool changed = false;
        auto assign = [&](const std::shared_ptr<Filesystem::AssetInfos>& info)
        {
            // The ID the manager knows the asset under (the key of its table), which is what every lookup goes through
            const Filesystem::AssetID newValue = info ? manager->GetIDFromNameInProject(info->baseInfos.nameInProject) : Filesystem::AssetID();
            DEBUG_INFO("Asset field " + id + " assign : " + (info ? info->baseInfos.nameInProject : std::string("<none>")) + " id " + std::to_string(newValue.GetAsInt()) + " / baseInfos.ID " + (info ? std::to_string(info->baseInfos.ID.GetAsInt()) : std::string("-")) + " (was " + currentName + ")");
            if (newValue != value)
            {
                value = newValue;
                changed = true;
            }
        };

        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttonSize = ImGui::GetFrameHeight();
        const float fieldWidth = ImGui::GetContentRegionAvail().x - 3.0f * (buttonSize + style.ItemInnerSpacing.x);

        // ---- The field
        const std::string label = (current ? currentName : std::string("None")) + id;
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        if (!current)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Button(label.c_str(), ImVec2(fieldWidth, 0.0f)))
            ImGui::OpenPopup("##AssetPicker");
        if (!current)
            ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s\nClick to pick an asset, or drop one from the browser", current ? currentName.c_str() : "No asset");

        // A compatible asset dragged over the field is highlighted, an incompatible one is refused
        if (ImGui::BeginDragDropTarget())
        {
            std::vector<std::string> dropped = DragDrop::AcceptAssetDragDropPayload();
            if (!dropped.empty())
                assign(resolve(dropped[0]));
            ImGui::EndDragDropTarget();
        }

        // ---- Picker
        ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 120.0f), ImVec2(FLT_MAX, 420.0f));
        if (ImGui::BeginPopup("##AssetPicker"))
        {
            static char filter[128] = "";
            if (ImGui::IsWindowAppearing())
            {
                filter[0] = '\0';
                ImGui::SetKeyboardFocusHere();
            }

            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##PickerFilter", ICON_LC_SEARCH "  Search...", filter, sizeof(filter));
            ImGui::Separator();

            if (ImGui::Selectable("None", !current))
            {
                assign(nullptr);
                ImGui::CloseCurrentPopup();
            }

            auto lower = [](std::string v) { std::transform(v.begin(), v.end(), v.begin(), ::tolower); return v; };
            const std::string needle = lower(filter);

            std::vector<std::shared_ptr<Filesystem::AssetInfos>> candidates;
            for (const auto& [assetId, info] : manager->AssetIDMap)
            {
                if (!info || info->baseInfos.isDirectory || info->baseInfos.nameInProject.empty())
                    continue;
                if (current && info->baseInfos.type != current->baseInfos.type)
                    continue;
                if (!needle.empty() && lower(info->baseInfos.nameInProject).find(needle) == std::string::npos)
                    continue;
                candidates.push_back(info);
            }
            std::sort(candidates.begin(), candidates.end(), [](const auto& l, const auto& r) { return l->baseInfos.nameInProject < r->baseInfos.nameInProject; });

            ImGui::BeginChild("##PickerList", ImVec2(0.0f, 260.0f));
            ImGuiListClipper clipper;
            clipper.Begin((int)candidates.size());
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                {
                    const auto& info = candidates[i];
                    const bool selected = info->baseInfos.ID == value;
                    if (ImGui::Selectable(info->baseInfos.nameInProject.c_str(), selected))
                    {
                        assign(info);
                        ImGui::CloseCurrentPopup();
                    }
                    if (selected && ImGui::IsWindowAppearing())
                        ImGui::SetScrollHereY();
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        // ---- Use the asset selected in the browser
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        const std::string selectedName = browser ? browser->GetSelectedAssetName() : std::string();
        const auto selectedInfo = resolve(selectedName);
        ImGui::BeginDisabled(!selectedInfo);
        if (ImGui::Button((std::string(ICON_LC_CIRCLE_ARROW_LEFT) + id + "Use").c_str(), ImVec2(buttonSize, buttonSize)))
            assign(selectedInfo);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        {
            if (selectedInfo)
                ImGui::SetTooltip("Use the asset selected in the browser\n%s", selectedName.c_str());
            else if (!selectedName.empty())
                ImGui::SetTooltip("The asset selected in the browser is not the type this field takes");
            else
                ImGui::SetTooltip("Select an asset in the browser to use it here");
        }

        // ---- Show it in the browser
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::BeginDisabled(!current || !browser);
        if (ImGui::Button((std::string(ICON_LC_FOLDER_SEARCH) + id + "Browse").c_str(), ImVec2(buttonSize, buttonSize)))
            browser->RevealAsset(currentName);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Browse to the asset in the Asset Browser");

        // ---- Clear
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::BeginDisabled(!current);
        if (ImGui::Button((std::string(ICON_LC_X) + id + "Clear").c_str(), ImVec2(buttonSize, buttonSize)))
            assign(nullptr);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Clear");

        return changed;
    }

    void PropertiesPanel::DrawField(const FieldInfo *field, void *value, std::shared_ptr<Engine::Objects::Components::Component> comp, const Container *container, const int valueIndexInContainer)
    {
        // A container element is labelled by its index : the string must outlive this function's widgets (a temporary's c_str() dangled)
        const std::string indexLabel = valueIndexInContainer != -1 ? std::to_string(valueIndexInContainer) : std::string();
        const char * fieldName = valueIndexInContainer != -1 ? indexLabel.c_str() : field->name;

        bool readOnly = (field->flags & ReadOnly) && !(field->flags & Editable);

        ImGui::TableNextRow();

        // Column 0 : Label
        ImGui::TableSetColumnIndex(0);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(fieldName);
        if(field->tooltip && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", field->tooltip);

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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::Bool:
            {
                bool* v = static_cast<bool*>(value);
                if (ImGui::Checkbox(id.c_str(), v))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::Asset:
            {
                Filesystem::AssetID* asset = static_cast<Filesystem::AssetID*>(value);
                if (asset && DrawAssetField(id, *asset, assetBrowser))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::Tag:
            {
                if (TagWidgets::TagPicker(id.c_str(), *static_cast<Engine::Core::Tag*>(value)))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::TagContainer:
            {
                if (TagWidgets::TagContainerEditor(id.c_str(), *static_cast<Engine::Core::TagContainer*>(value)))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::TagQuery:
            {
                if (TagWidgets::TagQueryInput(id.c_str(), *static_cast<Engine::Core::TagQuery*>(value)))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::Quat:
            {
                glm::quat* quat = static_cast<glm::quat*>(value);

                if (DrawQuatEuler(fieldName, *quat, field->min, field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::Enum:
            {
                // Zeroed first : an enum can be narrower than int (uint8_t), memcpy would leave the upper bytes as garbage
                int current = 0;
                memcpy(&current, value, field->enumDesc->size);
                if (current < 0 || current >= (int)field->enumDesc->values.size())
                    current = 0;

                std::vector<const char*> items;
                for (auto& e : field->enumDesc->values)
                    items.push_back(e.name);

                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo(id.c_str(), &current,
                                items.data(), items.size()))
                {
                    memcpy(value, &current, field->enumDesc->size);

                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            } 
            case TypeID::Vec3:
            {
                if (IsAbsolutePosition(field, comp, container))
                {
                    if (InputAbsolutePosition(id.c_str(), std::static_pointer_cast<Transform>(comp)))
                    {
                        MarkWorldDirty();
                    }
                    break;
                }

                glm::vec3* vec = static_cast<glm::vec3*>(value);

                if (InputVector3<float>(fieldName, &vec->x, 0.1f, field->min, field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::Vec2:
            {
                glm::vec2* vec = static_cast<glm::vec2*>(value);

                if (InputVector2<float>(fieldName, &vec->x, 0.1f, field->min, field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::IVec2:
            {
                glm::ivec2* vec = static_cast<glm::ivec2*>(value);

                if (InputVector2<int>(fieldName, &vec->x, 1.0f, (int)field->min, (int)field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            } 
            case TypeID::IVec3:
            {
                glm::ivec3* vec = static_cast<glm::ivec3*>(value);

                if (InputVector3<int>(fieldName, &vec->x, 1.0f, (int)field->min, (int)field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::IVec4:
            {
                glm::ivec2* vec = static_cast<glm::ivec2*>(value);

                if (InputVector4<int>(fieldName, &vec->x, 1.0f, (int)field->min, (int)field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::UVec2:
            {
                glm::uvec2* vec = static_cast<glm::uvec2*>(value);

                if (InputVector2<unsigned int>(fieldName, &vec->x, 1.0f, 0.0f, (int)field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            } 
            case TypeID::UVec3:
            {
                glm::uvec3* vec = static_cast<glm::uvec3*>(value);

                if (InputVector3<unsigned int>(fieldName, &vec->x, 1.0f, 0.0f, (int)field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::UVec4:
            {
                glm::uvec2* vec = static_cast<glm::uvec2*>(value);

                if (InputVector4<unsigned int>(fieldName, &vec->x, 1.0f, 0.0f, (int)field->max))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::Mat2:
            {
                glm::mat2* mat = static_cast<glm::mat2*>(value);

                if (InputMatrix(field->name, *mat))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::Mat3:
            {
                glm::mat3* mat = static_cast<glm::mat3*>(value);

                if (InputMatrix(field->name, *mat))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }
                break;
            }
            case TypeID::Mat4:
            {
                glm::mat4* mat = static_cast<glm::mat4*>(value);

                if (InputMatrix(field->name, *mat))
                {
                    FieldChangedEvent evt{ field };
                    comp->OnFieldChanged(evt); MarkWorldDirty();
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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
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
                    comp->OnFieldChanged(evt); MarkWorldDirty();
                }

                break;
            }

            case TypeID::Map:
            case TypeID::Struct:
            case TypeID::Unknown:
                break;
        }

        if(readOnly)
            ImGui::EndDisabled();
    }
}
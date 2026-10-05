#include "level_tree.hpp"

#include "engine/core/engine.hpp"

#include "apps/editor/gui/main_window.hpp"
#include "apps/editor/gui/panels/common.hpp"
#include "apps/editor/gui/dragdrop/asset_drag_drop.hpp"

#include "engine/world/levels/level_manager.hpp"
#include "engine/renderer/components/model_component.hpp"

namespace Shard::Editor::GUI{

    using Engine::Core::GetEngine;

    // Payload for dragging an actor row within the outliner itself (reparenting), as opposed to
    // DragDrop::kAssetPayloadType which carries assets dragged in from the asset browser.
    static constexpr const char* kOutlinerActorPayloadType = "SHARD_OUTLINER_ACTOR";

    void LevelTree::Draw()
    {
        auto level = GetEngine().GetLevelManager()->GetLevelAt(0);

        // Stable ImGui ID (###LevelTree) so the title can carry the level's name (and an unsaved-
        // changes marker) without losing this panel's saved dock position/size every time the level
        // name changes or a different level is loaded.
        std::string title = "Level";
        if (level)
        {
            title += " - " + level->GetName();
            if (level->IsDirty())
                title += "*";
        }
        title += "###LevelTree";

        if (!ImGui::Begin(title.c_str()))
        {
            ImGui::End();
            return;
        }

        if (!level)
        {
            ImGui::Text("No level loaded");
            ImGui::End();
            return;
        }

        for (const auto& [id, rootActor] : level->GetRootActors())
        {
            if (rootActor)
                DrawActorNode(rootActor);
        }

        // Whole-window drop zone (rather than per-item) so dropping a mesh anywhere in the outliner -
        // not just precisely on an existing row - spawns it as a new root actor; BeginDragDropTargetCustom
        // registers the window's own rect as a target without submitting an "item", so it doesn't
        // interfere with the "click empty space to deselect" hit-testing below.
        if (ImGuiWindow* window = ImGui::GetCurrentWindow())
        {
            if (ImGui::BeginDragDropTargetCustom(window->Rect(), window->ID))
            {
                std::vector<std::string> dropped = DragDrop::AcceptAssetDragDropPayload();
                for (const auto& nameInProject : dropped)
                {
                    if (Engine::Filesystem::Path(nameInProject).GetExtensionType() != Engine::Filesystem::Type::T_MODEL)
                        continue;

                    auto actor = Engine::Core::Object::CreateWithContext<Engine::Objects::Actor>(
                        &GetEngine(), Engine::Filesystem::Path(nameInProject).GetFilename(false), &GetEngine());
                    level->AddActor(actor);

                    auto model = actor->AddComponent<Engine::Objects::Components::Model>();
                    if (model)
                        model->SetMesh(nameInProject);

                    selectedID = actor->GetID();
                    if (parent)
                        parent->SetSelectedActor(actor);

                    level->SetDirty(true);
                }

                // Dropped on empty space rather than on a specific row -> detach from whatever
                // actor it was parented under and make it a root actor of the level.
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kOutlinerActorPayloadType))
                {
                    int droppedIdInt = *(const int*)payload->Data;
                    auto droppedActor = std::dynamic_pointer_cast<Engine::Objects::Actor>(
                        GetEngine().GetObjectIDManager()->GetObjectFromID(Engine::Core::ObjectID(droppedIdInt)));
                    if (droppedActor)
                        ReparentActor(droppedActor, nullptr);
                }

                ImGui::EndDragDropTarget();
            }
        }

        // Right-click on empty space (including a level with no actors at all) -> create a root actor.
        // NoOpenOverItems lets each actor node keep its own context menu (see DrawActorNode).
        if (ImGui::BeginPopupContextWindow("##LevelTreeContext",
                ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Create Actor"))
            {
                auto actor = Engine::Core::Object::CreateWithContext<Engine::Objects::Actor>(
                    &GetEngine(), "New Actor", &GetEngine());
                level->AddActor(actor);
                selectedID = actor->GetID();
                if (parent)
                    parent->SetSelectedActor(actor);

                level->SetDirty(true);
            }

            ImGui::EndPopup();
        }

        // Click vide = deselect
        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
        {
            if (!ImGui::IsAnyItemHovered())
            {
                selectedID = {};
                if (parent)
                    parent->SetSelectedActor(nullptr);
            }
        }

        ImGui::End();
    }
    

    void LevelTree::SetParentWindow(Core::EditorMainWindow *parent)
    {
        this->parent = parent;
    }

    void LevelTree::SetSelection(std::shared_ptr<Engine::Objects::Actor> actor)
    {
        if (actor)
        {
            selectedID = actor->GetID();
        }
        else
        {
            selectedID = {};
        }
    }

    void LevelTree::DrawActorNode(std::shared_ptr<Engine::Objects::Actor> actor)
    {
        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanFullWidth;

        if (actor->GetChildrenID(false).empty())
            flags |= ImGuiTreeNodeFlags_Leaf;

        if (selectedID == actor->GetID())
            flags |= ImGuiTreeNodeFlags_Selected;

        bool open = ImGui::TreeNodeEx(
            (void*)(intptr_t)actor->GetID().GetAsInt(),
            flags,
            actor->GetName().c_str()
        );

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            selectedID = actor->GetID();
            if (parent)
                parent->SetSelectedActor(actor);
        }

        // Drag source: lets this row be picked up and dropped elsewhere in the outliner to reparent it.
        if (ImGui::BeginDragDropSource())
        {
            int actorIdInt = actor->GetID().GetAsInt();
            ImGui::SetDragDropPayload(kOutlinerActorPayloadType, &actorIdInt, sizeof(int));
            ImGui::Text("%s", actor->GetName().c_str());
            ImGui::EndDragDropSource();
        }

        // Drop target: dropping another outliner row here reparents it under this actor.
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kOutlinerActorPayloadType))
            {
                int droppedIdInt = *(const int*)payload->Data;
                auto droppedActor = std::dynamic_pointer_cast<Engine::Objects::Actor>(
                    Engine::Core::GetEngine().GetObjectIDManager()->GetObjectFromID(Engine::Core::ObjectID(droppedIdInt)));
                if (droppedActor)
                    ReparentActor(droppedActor, actor);
            }

            ImGui::EndDragDropTarget();
        }

        // Context Menu
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Rename"))
            {
                renamingActor = actor;
                strcpy(renameBuffer, actor->GetName().c_str());
                openRenamingPopup = true;
            }

            if (ImGui::MenuItem("Delete"))
            {
                if (actor == parent->GetSelectedActor())
                    parent->SetSelectedActor(nullptr);

                actor->Destroy();

                if (auto* level = GetEngine().GetLevelManager()->GetLevelAt(0))
                    level->SetDirty(true);
            }

            if (ImGui::MenuItem("Duplicate"))
            {
                actor->Clone();

                if (auto* level = GetEngine().GetLevelManager()->GetLevelAt(0))
                    level->SetDirty(true);
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Create Actor"))
            {
                auto child = Engine::Core::Object::CreateWithContext<Engine::Objects::Actor>(&Engine::Core::GetEngine(), "New Actor", &Engine::Core::GetEngine());
                actor->AddChild(child);

                if (auto* level = GetEngine().GetLevelManager()->GetLevelAt(0))
                    level->SetDirty(true);
            }

            ImGui::EndPopup();
        }

        if(openRenamingPopup){
            ImGui::OpenPopup("RenameActorPopup");
            openRenamingPopup = false;
        }

        // Rename Popup
        if (ImGui::BeginPopup("RenameActorPopup"))
        {
            ImGui::InputText("##ActorName", renameBuffer, sizeof(renameBuffer));

            if (ImGui::Button("OK") || Engine::Core::GetEngine().GetInputManager()->WasKeyReleased(Engine::Input::Key::Enter))
            {
                if (renamingActor)
                {
                    renamingActor->SetName(renameBuffer);

                    if (auto* level = GetEngine().GetLevelManager()->GetLevelAt(0))
                        level->SetDirty(true);
                }

                renamingActor = nullptr;
                openRenamingPopup = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
            {
                renamingActor = nullptr;
                openRenamingPopup = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (open)
        {
            for (auto childID : actor->GetChildrenID(false))
            {
                auto obj = actor->GetChild(childID);
                auto child = std::dynamic_pointer_cast<Engine::Objects::Actor>(obj);
                if (child)
                    DrawActorNode(child);
            }

            ImGui::TreePop();
        }
    }

    void LevelTree::ReparentActor(std::shared_ptr<Engine::Objects::Actor> actor, std::shared_ptr<Engine::Objects::Actor> newParent)
    {
        if (!actor || actor == newParent)
            return;

        // Cycle detection and world-transform preservation ("attach in place") both live in
        // Actor::SetParent now, shared with any other caller (scripts, etc).
        actor->SetParent(newParent);

        if (auto* level = GetEngine().GetLevelManager()->GetLevelAt(0))
            level->SetDirty(true);
    }
}


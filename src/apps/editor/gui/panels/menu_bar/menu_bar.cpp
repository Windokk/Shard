#include "menu_bar.hpp"

#include "apps/editor/gui/main_window.hpp"
#include "apps/editor/gui/popups.hpp"
#include "apps/editor/commands/command_stack.hpp"

#include "engine/world/engine.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/world/world.hpp"
#include "engine/assets/project/project.hpp"

#include "imgui/imgui.h"
#include "ImGuizmo.h"

#include <cstring>

namespace Shard::Editor::GUI{

    using Engine::Core::GetEngine;

    static void MarkWorldDirty()
    {
        if (auto* world = GetEngine().GetWorldManager()->GetWorldAt(0))
            world->SetDirty(true);
    }

    void MenuBar::SetParentWindow(Core::EditorMainWindow *parent)
    {
        this->parent = parent;
    }

    void MenuBar::Draw()
    {
        if (!ImGui::BeginMainMenuBar())
            return;

        DrawFileMenu();
        DrawEditMenu();
        DrawSelectMenu();
        DrawToolsMenu();
        DrawWindowMenu();
        DrawHelpMenu();

        ImGui::EndMainMenuBar();

        DrawSaveAsPopup();
        DrawRenamePopup();
        DrawAboutPopup();
    }

    void MenuBar::DrawFileMenu()
    {
        if (!ImGui::BeginMenu("File"))
            return;

        if (ImGui::MenuItem("New World"))
        {
            auto* current = GetEngine().GetWorldManager()->GetWorldAt(0);
            if (current && current->IsDirty())
            {
                GUI::Popups::ConfirmUnsavedChanges(current->GetName(),
                    [this](){ SaveCurrentWorld(); CreateNewWorld(); },
                    [this](){ CreateNewWorld(); });
            }
            else
            {
                CreateNewWorld();
            }
        }

        if (ImGui::MenuItem("Save World", "Ctrl+S"))
            SaveCurrentWorld();

        if (ImGui::MenuItem("Save World As..."))
        {
            auto world = GetEngine().GetWorldManager()->GetWorldAt(0);
            if (world)
            {
                strncpy(saveAsNameBuffer, world->GetName().c_str(), sizeof(saveAsNameBuffer) - 1);
                saveAsNameBuffer[sizeof(saveAsNameBuffer) - 1] = '\0';
                openSaveAsPopup = true;
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Project Settings..."))
            parent->panelVisibility.projectSettings = true;

        ImGui::Separator();

        if (ImGui::MenuItem("Exit"))
        {
            auto* current = GetEngine().GetWorldManager()->GetWorldAt(0);
            if (current && current->IsDirty())
            {
                GUI::Popups::ConfirmUnsavedChanges(current->GetName(),
                    [this](){ SaveCurrentWorld(); parent->RequestExit(); },
                    [this](){ parent->RequestExit(); });
            }
            else
            {
                parent->RequestExit();
            }
        }

        ImGui::EndMenu();
    }

    void MenuBar::CreateNewWorld()
    {
        auto path = Engine::Filesystem::Path(
            GetEngine().GetCurrentProject()->GetProjectResourcesPath().full + "/NewWorld.world", true);

        GetEngine().GetBuildSettings()->AddToBuildSettings(path);

        auto world = std::make_shared<Engine::Worlds::World>("NewWorld", path);
        world->SetBuildIndex(GetEngine().GetBuildSettings()->GetWorldBuildIndex(path));
        world->Serialize(path);

        GetEngine().GetWorldManager()->LoadWorld(world);
        parent->SetSelectedActor(nullptr);
    }

    void MenuBar::SaveCurrentWorld()
    {
        auto world = GetEngine().GetWorldManager()->GetWorldAt(0);
        if (world)
            world->Serialize(world->GetPath());
    }

    void MenuBar::DrawSaveAsPopup()
    {
        if (openSaveAsPopup)
        {
            ImGui::OpenPopup("Save World As");
            openSaveAsPopup = false;
        }

        if (ImGui::BeginPopup("Save World As"))
        {
            ImGui::InputText("##SaveAsName", saveAsNameBuffer, sizeof(saveAsNameBuffer));

            if (ImGui::Button("Save"))
            {
                auto world = GetEngine().GetWorldManager()->GetWorldAt(0);
                if (world)
                {
                    auto newPath = Engine::Filesystem::Path(
                        world->GetPath().GetParent() + "/" + std::string(saveAsNameBuffer) + ".world", true);

                    GetEngine().GetBuildSettings()->AddToBuildSettings(newPath);
                    world->Serialize(newPath);
                }

                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();

            ImGui::TextDisabled("Saves a copy; keeps editing the original world.");

            ImGui::EndPopup();
        }
    }

    void MenuBar::DrawEditMenu()
    {
        if (!ImGui::BeginMenu("Edit"))
            return;

        auto selected = parent->GetSelectedActor();

        if (ImGui::MenuItem("Undo", "Ctrl+W"))
            Commands::CommandStack::Get().Undo();

        if (ImGui::MenuItem("Redo", "Ctrl+Y"))
            Commands::CommandStack::Get().Redo();

        ImGui::Separator();

        if (ImGui::MenuItem("Copy", nullptr, false, selected != nullptr))
            clipboardActor = selected;

        if (ImGui::MenuItem("Cut", nullptr, false, selected != nullptr))
        {
            clipboardActor = selected->Clone();
            selected->Destroy();
            parent->SetSelectedActor(clipboardActor);
            MarkWorldDirty();
        }

        if (ImGui::MenuItem("Paste", nullptr, false, clipboardActor != nullptr))
        {
            parent->SetSelectedActor(clipboardActor->Clone());
            MarkWorldDirty();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Duplicate", nullptr, false, selected != nullptr))
        {
            parent->SetSelectedActor(selected->Clone());
            MarkWorldDirty();
        }

        if (ImGui::MenuItem("Rename", nullptr, false, selected != nullptr))
        {
            strncpy(renameBuffer, selected->GetName().c_str(), sizeof(renameBuffer) - 1);
            renameBuffer[sizeof(renameBuffer) - 1] = '\0';
            openRenamingPopup = true;
        }

        if (ImGui::MenuItem("Delete", nullptr, false, selected != nullptr))
        {
            parent->SetSelectedActor(nullptr);
            selected->Destroy();
            MarkWorldDirty();
        }

        ImGui::EndMenu();
    }

    void MenuBar::DrawRenamePopup()
    {
        if (openRenamingPopup)
        {
            ImGui::OpenPopup("Rename Actor##MenuBar");
            openRenamingPopup = false;
        }

        if (ImGui::BeginPopup("Rename Actor##MenuBar"))
        {
            ImGui::InputText("##RenameActor", renameBuffer, sizeof(renameBuffer));

            if (ImGui::Button("OK"))
            {
                auto selected = parent->GetSelectedActor();
                if (selected)
                {
                    selected->SetName(renameBuffer);
                    MarkWorldDirty();
                }

                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    void MenuBar::DrawSelectMenu()
    {
        if (!ImGui::BeginMenu("Select"))
            return;

        if (ImGui::MenuItem("Deselect All", nullptr, false, parent->GetSelectedActor() != nullptr))
            parent->SetSelectedActor(nullptr);

        ImGui::EndMenu();
    }

    void MenuBar::DrawToolsMenu()
    {
        if (!ImGui::BeginMenu("Tools"))
            return;

        auto op = parent->viewport->GetGizmoOperation();

        if (ImGui::MenuItem("Move Tool", nullptr, op == ImGuizmo::TRANSLATE))
            parent->viewport->SetGizmoOperation(ImGuizmo::TRANSLATE);

        if (ImGui::MenuItem("Rotate Tool", nullptr, op == ImGuizmo::ROTATE))
            parent->viewport->SetGizmoOperation(ImGuizmo::ROTATE);

        if (ImGui::MenuItem("Scale Tool", nullptr, op == ImGuizmo::SCALE))
            parent->viewport->SetGizmoOperation(ImGuizmo::SCALE);

        ImGui::Separator();

        if (ImGui::MenuItem("Toggle Gizmos", nullptr, parent->settings.showGizmos))
            parent->settings.showGizmos = !parent->settings.showGizmos;

        ImGui::Separator();

        bool playing = GetEngine().IsInPlayMode();
        if (ImGui::MenuItem("Play Mode", nullptr, playing))
            GetEngine().SetPlayMode(!playing);

        ImGui::EndMenu();
    }

    void MenuBar::DrawWindowMenu()
    {
        if (!ImGui::BeginMenu("Window"))
            return;

        ImGui::MenuItem("Viewport", nullptr, &parent->panelVisibility.viewport);
        ImGui::MenuItem("World Tree", nullptr, &parent->panelVisibility.worldTree);
        ImGui::MenuItem("World Settings", nullptr, &parent->panelVisibility.worldSettings);
        ImGui::MenuItem("Properties", nullptr, &parent->panelVisibility.properties);
        ImGui::MenuItem("Asset Browser", nullptr, &parent->panelVisibility.assetBrowser);
        ImGui::MenuItem("Console", nullptr, &parent->panelVisibility.console);
        ImGui::MenuItem("Profiler", nullptr, &parent->panelVisibility.profiler);

        ImGui::EndMenu();
    }

    void MenuBar::DrawHelpMenu()
    {
        if (!ImGui::BeginMenu("Help"))
            return;

        if (ImGui::MenuItem("About Shard"))
            openAboutPopup = true;

        ImGui::EndMenu();
    }

    void MenuBar::DrawAboutPopup()
    {
        if (openAboutPopup)
        {
            ImGui::OpenPopup("About Shard##MenuBar");
            openAboutPopup = false;
        }

        ImGui::SetNextWindowSize(ImVec2(360, 0));
        if (ImGui::BeginPopupModal("About Shard##MenuBar", nullptr, ImGuiWindowFlags_NoResize))
        {
            ImGui::TextUnformatted("Shard");
            ImGui::TextDisabled("A game engine and editor.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("OK", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }
}

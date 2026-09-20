#include "menu_bar.hpp"

#include "editor/gui/main_window.hpp"
#include "editor/gui/popups.hpp"
#include "editor/commands/command_stack.hpp"

#include "engine/core/engine.hpp"
#include "engine/levels/level_manager.hpp"
#include "engine/levels/level.hpp"
#include "engine/projects/project.hpp"

#include "imgui/imgui.h"
#include "ImGuizmo.h"

#include <cstring>

namespace Shard::Editor::GUI{

    using Engine::Core::GetEngine;

    static void MarkLevelDirty()
    {
        if (auto* level = GetEngine().GetLevelManager()->GetLevelAt(0))
            level->SetDirty(true);
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

        if (ImGui::MenuItem("New Level"))
        {
            auto* current = GetEngine().GetLevelManager()->GetLevelAt(0);
            if (current && current->IsDirty())
            {
                GUI::Popups::ConfirmUnsavedChanges(current->GetName(),
                    [this](){ SaveCurrentLevel(); CreateNewLevel(); },
                    [this](){ CreateNewLevel(); });
            }
            else
            {
                CreateNewLevel();
            }
        }

        if (ImGui::MenuItem("Save Level", "Ctrl+S"))
            SaveCurrentLevel();

        if (ImGui::MenuItem("Save Level As..."))
        {
            auto level = GetEngine().GetLevelManager()->GetLevelAt(0);
            if (level)
            {
                strncpy(saveAsNameBuffer, level->GetName().c_str(), sizeof(saveAsNameBuffer) - 1);
                saveAsNameBuffer[sizeof(saveAsNameBuffer) - 1] = '\0';
                openSaveAsPopup = true;
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Exit"))
        {
            auto* current = GetEngine().GetLevelManager()->GetLevelAt(0);
            if (current && current->IsDirty())
            {
                GUI::Popups::ConfirmUnsavedChanges(current->GetName(),
                    [this](){ SaveCurrentLevel(); parent->RequestExit(); },
                    [this](){ parent->RequestExit(); });
            }
            else
            {
                parent->RequestExit();
            }
        }

        ImGui::EndMenu();
    }

    void MenuBar::CreateNewLevel()
    {
        auto path = Engine::Filesystem::Path(
            GetEngine().GetCurrentProject()->GetProjectResourcesPath().full + "/NewLevel.lvl", true);

        GetEngine().GetBuildSettings()->AddToBuildSettings(path);

        auto level = std::make_shared<Engine::Levels::Level>("NewLevel", path);
        level->SetBuildIndex(GetEngine().GetBuildSettings()->GetLevelBuildIndex(path));
        level->Serialize(path);

        GetEngine().GetLevelManager()->LoadLevel(level);
        parent->SetSelectedActor(nullptr);
    }

    void MenuBar::SaveCurrentLevel()
    {
        auto level = GetEngine().GetLevelManager()->GetLevelAt(0);
        if (level)
            level->Serialize(level->GetPath());
    }

    void MenuBar::DrawSaveAsPopup()
    {
        if (openSaveAsPopup)
        {
            ImGui::OpenPopup("Save Level As");
            openSaveAsPopup = false;
        }

        if (ImGui::BeginPopup("Save Level As"))
        {
            ImGui::InputText("##SaveAsName", saveAsNameBuffer, sizeof(saveAsNameBuffer));

            if (ImGui::Button("Save"))
            {
                auto level = GetEngine().GetLevelManager()->GetLevelAt(0);
                if (level)
                {
                    auto newPath = Engine::Filesystem::Path(
                        level->GetPath().GetParent() + "/" + std::string(saveAsNameBuffer) + ".lvl", true);

                    GetEngine().GetBuildSettings()->AddToBuildSettings(newPath);
                    level->Serialize(newPath);
                }

                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();

            ImGui::TextDisabled("Saves a copy; keeps editing the original level.");

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
            MarkLevelDirty();
        }

        if (ImGui::MenuItem("Paste", nullptr, false, clipboardActor != nullptr))
        {
            parent->SetSelectedActor(clipboardActor->Clone());
            MarkLevelDirty();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Duplicate", nullptr, false, selected != nullptr))
        {
            parent->SetSelectedActor(selected->Clone());
            MarkLevelDirty();
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
            MarkLevelDirty();
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
                    MarkLevelDirty();
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
        ImGui::MenuItem("Level Tree", nullptr, &parent->panelVisibility.levelTree);
        ImGui::MenuItem("Level Settings", nullptr, &parent->panelVisibility.levelSettings);
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

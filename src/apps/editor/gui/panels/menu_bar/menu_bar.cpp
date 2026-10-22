#include "menu_bar.hpp"
#include "engine/plugins/manager.hpp"

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

        const bool hasSelection = parent->HasSelection();

        auto& history = Commands::CommandStack::Get();

        if (ImGui::MenuItem("Undo", "Ctrl+W", false, history.CanUndo()))
            history.Undo();

        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, history.CanRedo()))
            history.Redo();

        ImGui::Separator();

        // Shortcuts : the ones of the outliner and the viewport, whichever has the focus
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection))
            parent->CopySelection();

        if (ImGui::MenuItem("Cut", "Ctrl+X", false, hasSelection))
            parent->CutSelection();

        if (ImGui::MenuItem("Paste", "Ctrl+V", false, parent->CanPaste()))
            parent->PasteClipboard();

        ImGui::Separator();

        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection))
            parent->DuplicateSelection();

        if (ImGui::MenuItem("Rename", "F2", false, hasSelection))
            parent->RenameSelection();

        if (ImGui::MenuItem("Group", "Ctrl+G", false, hasSelection))
            parent->GroupSelection();

        if (ImGui::MenuItem("Delete", "Del", false, hasSelection))
            parent->DeleteSelection();

        ImGui::Separator();

        if (ImGui::MenuItem("Project Settings..."))
            parent->panelVisibility.projectSettings = true;

        if (ImGui::MenuItem("Editor Preferences..."))
            parent->panelVisibility.editorPreferences = true;

        ImGui::EndMenu();
    }

    void MenuBar::DrawSelectMenu()
    {
        if (!ImGui::BeginMenu("Select"))
            return;

        const bool hasSelection = parent->HasSelection();

        if (ImGui::MenuItem("Select All", "Ctrl+A"))
            parent->SelectAll();

        if (ImGui::MenuItem("Deselect All", "Esc", false, hasSelection))
            parent->ClearSelection();

        if (ImGui::MenuItem("Invert Selection"))
            parent->InvertSelection();

        if (ImGui::MenuItem("Select Children", nullptr, false, hasSelection))
            parent->SelectChildren();

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

        bool hasSelection = parent->HasSelection();

        if (ImGui::MenuItem("Focus Selected", "F", false, hasSelection))
            parent->viewport->FocusSelected();

        if (ImGui::MenuItem("Snap To Floor", "End", false, hasSelection))
            parent->viewport->SnapSelectedToFloor();

        ImGui::Separator();

        if (ImGui::MenuItem("Toggle Gizmos", nullptr, parent->settings.showGizmos))
            parent->settings.showGizmos = !parent->settings.showGizmos;

        ImGui::Separator();

        bool playing = GetEngine().IsInPlayMode();
        if (ImGui::MenuItem("Play Mode", playing ? "F1 : stop" : nullptr, playing))
        {
            if (playing)
                GetEngine().SetPlayMode(false);
            else
                parent->RequestPlay();
        }

        ImGui::EndMenu();
    }

    void MenuBar::DrawWindowMenu()
    {
        if (!ImGui::BeginMenu("Window"))
            return;

        if (ImGui::MenuItem("Game View", "G", parent->IsGameView(), !GetEngine().IsInPlayMode()))
            parent->ToggleGameView();

        ImGui::Separator();

        ImGui::MenuItem("Viewport", nullptr, &parent->panelVisibility.viewport);
        ImGui::MenuItem("World Tree", nullptr, &parent->panelVisibility.worldTree);
        ImGui::MenuItem("World Settings", nullptr, &parent->panelVisibility.worldSettings);
        ImGui::MenuItem("Properties", nullptr, &parent->panelVisibility.properties);
        ImGui::MenuItem("Asset Browser", nullptr, &parent->panelVisibility.assetBrowser);
        ImGui::MenuItem("Console", nullptr, &parent->panelVisibility.console);
        ImGui::MenuItem("Profiler", nullptr, &parent->panelVisibility.profiler);
        ImGui::MenuItem("Plugins", nullptr, &parent->panelVisibility.plugins);
        for(const auto& panel : Engine::Plugins::PluginManager::Get().Extensions().Get<Engine::Plugins::EditorPanelItem>("editor.panel"))
            ImGui::MenuItem(panel->title.c_str(), nullptr, &panel->visible);

        ImGui::Separator();

        ImGui::MenuItem("Project Settings", nullptr, &parent->panelVisibility.projectSettings);
        ImGui::MenuItem("Editor Preferences", nullptr, &parent->panelVisibility.editorPreferences);

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

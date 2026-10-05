#pragma once

#include "engine/world/actor.hpp"

namespace Shard::Editor::Core{

    class EditorMainWindow;
}

namespace Shard::Editor::GUI{

    class MenuBar
    {
        public:
            void Draw();

            void SetParentWindow(Core::EditorMainWindow* parent);

        private:
            void DrawFileMenu();
            void DrawEditMenu();
            void DrawSelectMenu();
            void DrawToolsMenu();
            void DrawWindowMenu();
            void DrawHelpMenu();

            void DrawSaveAsPopup();
            void DrawRenamePopup();
            void DrawAboutPopup();

            // Shared by "New Level" and "Exit", both of which need to warn about (and optionally
            // save) unsaved changes on the current level before proceeding.
            void CreateNewLevel();
            void SaveCurrentLevel();

            Core::EditorMainWindow* parent = nullptr;

            // Copy/Cut/Paste
            std::shared_ptr<Engine::Objects::Actor> clipboardActor = nullptr;

            bool openSaveAsPopup = false;
            char saveAsNameBuffer[256] = "NewLevel";

            bool openRenamingPopup = false;
            char renameBuffer[256] = "";

            bool openAboutPopup = false;
    };
}

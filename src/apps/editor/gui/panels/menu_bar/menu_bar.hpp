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

            // Shared by "New World" and "Exit", both of which need to warn about (and optionally
            // save) unsaved changes on the current world before proceeding.
            void CreateNewWorld();
            void SaveCurrentWorld();

            Core::EditorMainWindow* parent = nullptr;

            // Copy/Cut/Paste
            std::shared_ptr<Engine::Objects::Actor> clipboardActor = nullptr;

            bool openSaveAsPopup = false;
            char saveAsNameBuffer[256] = "NewWorld";

            bool openRenamingPopup = false;
            char renameBuffer[256] = "";

            bool openAboutPopup = false;
    };
}

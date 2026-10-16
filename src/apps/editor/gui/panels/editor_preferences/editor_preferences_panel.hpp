#pragma once

#include "apps/editor/gui/panels/settings_view.hpp"

namespace Shard::Editor::Core{
    class EditorMainWindow;
}

namespace Shard::Editor::GUI{

    /// @brief Unreal-style "Editor Preferences" window : how the editor looks and behaves for this user, whatever the project.
    /// Edits EditorMainWindow::preferences in place ; the main window writes them to their file when they change (see
    /// Core::EditorPreferences), so there is nothing to save by hand.
    class EditorPreferencesPanel
    {
        public:
            EditorPreferencesPanel();

            void SetParentWindow(Core::EditorMainWindow* parent) { m_Parent = parent; }

            void Draw(bool* open);

        private:
            void DrawAppearanceCategory();
            void DrawViewportCategory();
            void DrawSnappingCategory();
            void DrawSavingCategory();

            Core::EditorMainWindow* m_Parent = nullptr;
            SettingsView m_View;

            // The scale being dragged : applied when the slider is let go, or the slider would move under the mouse
            float m_PendingUIScale = -1.0f;
    };
}

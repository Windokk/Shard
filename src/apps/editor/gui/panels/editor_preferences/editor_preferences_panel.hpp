#pragma once

#include "apps/editor/gui/panels/settings_view.hpp"

namespace Shard::Editor::Core{
    class EditorMainWindow;
}

namespace Shard::Editor::GUI{

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

            float m_PendingUIScale = -1.0f;
    };
}

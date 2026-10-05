#pragma once

namespace Shard::Editor::GUI{

    /// Unreal-style "Project Settings" window : a category list on the left, that category's
    /// settings on the right, and a search box filtering the settings shown.
    class ProjectSettingsPanel
    {
        public:
            void Draw(bool* open);

        private:
            enum class Category { General, Build, Physics };

            void DrawGeneralCategory();
            void DrawBuildCategory();
            void DrawPhysicsCategory();

            /// True when `label` passes the search box (always true when it's empty).
            bool Matches(const char* label) const;

            /// Label + tooltip on the left, widget filling the rest, Unreal's two-column row layout.
            bool BeginRow(const char* label, const char* tooltip = nullptr);

            void Save();

            Category m_Category = Category::General;
            char m_Search[128] = "";
            bool m_Dirty = false;
    };
}

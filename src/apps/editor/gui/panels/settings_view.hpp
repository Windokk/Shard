#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Shard::Editor::GUI{

    /// @brief The layout the Project Settings and the Editor Preferences share (Unreal's) : the categories on the left, the
    /// settings of the selected one on the right. While the search box has text, every category is drawn one after the
    /// other with only the settings that match, so a setting is found without knowing where it lives.
    ///
    /// The categories draw their settings with Section and BeginRow :
    ///
    ///     view.Section("Simulation");
    ///     bool reset = false;
    ///     if (view.BeginRow("Gravity", "Acceleration of every dynamic body", gravity != defaults.gravity, &reset))
    ///     {
    ///         if (reset) gravity = defaults.gravity;
    ///         ImGui::DragFloat3("##Gravity", &gravity.x);
    ///     }
    class SettingsView
    {
        public:
            struct Category{
                std::string name;
                std::string icon;           ///< an IconsLucide glyph shown before the name, may be empty
                std::function<void()> draw;
            };

            void SetCategories(std::vector<Category> categories) { m_Categories = std::move(categories); }

            /// The search box, as wide as the line minus `trailingWidth` (room for the caller's buttons on its right)
            void DrawSearchBar(float trailingWidth = 0.0f);

            /// The category list and the settings, filling what is left of the window
            void DrawBody();

            /// True when the search box is empty or `text` contains what it holds (case does not matter)
            bool Matches(const char* text) const;
            bool IsSearching() const { return m_Search[0] != '\0'; }
            /// The search names the section or the category being drawn : all of it is shown
            bool GroupMatches() const;

            /// @brief A titled group of rows. Only drawn when one of its rows is (the title waits for the first row that matches).
            void Section(const char* title);

            /// @brief The label (and its tooltip) in the left column, then sets the width of the widget that follows to the
            /// rest of the line. False when the row does not match the search : draw nothing then.
            /// @param modified the value differs from its default : a reset arrow is shown, and `*reset` is set when it is clicked
            bool BeginRow(const char* label, const char* tooltip = nullptr, bool modified = false, bool* reset = nullptr);

            /// @brief Free content (a list, buttons) that belongs to the current section : shown when the search is empty or
            /// matches one of `keywords`. Draws the pending section title when it is.
            bool BeginBlock(const char* keywords);

            // Rows of the common kinds : label, reset arrow while the value is not `defaultValue`, widget. True when the
            // value changed (edited or reset), so the caller can apply it.
            bool Checkbox(const char* label, const char* tooltip, bool& value, bool defaultValue);
            bool DragFloat(const char* label, const char* tooltip, float& value, float defaultValue,
                           float speed, float min, float max, const char* format = "%.3f");
            bool SliderFloat(const char* label, const char* tooltip, float& value, float defaultValue,
                             float min, float max, const char* format = "%.3f");
            bool DragInt(const char* label, const char* tooltip, int& value, int defaultValue,
                         float speed, int min, int max, const char* format = "%d");
            bool Combo(const char* label, const char* tooltip, int& value, int defaultValue, const char* const items[], int count);

        private:
            /// The ImGui id of a row's widget : the label alone is not unique, two sections can share it
            std::string WidgetId(const char* label) const;
            void FlushPendingHeaders();
            void DrawCategory(size_t index);

            std::vector<Category> m_Categories;
            size_t m_Selected = 0;
            char m_Search[128] = "";

            // Titles not drawn yet : they are drawn above the first row of theirs that matches the search
            const char* m_PendingSection = nullptr;
            const char* m_PendingCategory = nullptr;
            const char* m_CurrentSection = nullptr;
            const char* m_CurrentCategory = nullptr;
            int m_RowsDrawn = 0;
    };
}

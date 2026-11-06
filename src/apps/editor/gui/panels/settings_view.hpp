#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Shard::Editor::GUI{

    // Layout shared by Project Settings and Editor Preferences, categories on the left, settings on the right. While the search box has text every category is drawn with only its matching rows.
    class SettingsView
    {
        public:
            struct Category{
                std::string name;
                std::string icon;
                std::function<void()> draw;
            };

            void SetCategories(std::vector<Category> categories) { m_Categories = std::move(categories); }

            void DrawSearchBar(float trailingWidth = 0.0f);

            void DrawBody();

            bool Matches(const char* text) const;
            bool IsSearching() const { return m_Search[0] != '\0'; }
            bool GroupMatches() const;

            void Section(const char* title);

            bool BeginRow(const char* label, const char* tooltip = nullptr, bool modified = false, bool* reset = nullptr);

            bool BeginBlock(const char* keywords);

            bool Checkbox(const char* label, const char* tooltip, bool& value, bool defaultValue);
            bool DragFloat(const char* label, const char* tooltip, float& value, float defaultValue,
                           float speed, float min, float max, const char* format = "%.3f");
            bool SliderFloat(const char* label, const char* tooltip, float& value, float defaultValue,
                             float min, float max, const char* format = "%.3f");
            bool DragInt(const char* label, const char* tooltip, int& value, int defaultValue,
                         float speed, int min, int max, const char* format = "%d");
            bool Combo(const char* label, const char* tooltip, int& value, int defaultValue, const char* const items[], int count);

        private:
            std::string WidgetId(const char* label) const;
            void FlushPendingHeaders();
            void DrawCategory(size_t index);

            std::vector<Category> m_Categories;
            size_t m_Selected = 0;
            char m_Search[128] = "";

            const char* m_PendingSection = nullptr;
            const char* m_PendingCategory = nullptr;
            const char* m_CurrentSection = nullptr;
            const char* m_CurrentCategory = nullptr;
            int m_RowsDrawn = 0;
    };
}

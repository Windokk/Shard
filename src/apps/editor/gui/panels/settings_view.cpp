#include "settings_view.hpp"

#include "apps/editor/gui/IconsLucide.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cfloat>

namespace Shard::Editor::GUI{

    namespace {

        bool ContainsNoCase(const char* haystack, const char* needle)
        {
            if (!haystack)
                return false;
            std::string h = haystack;
            std::string n = needle;
            auto lower = [](unsigned char c){ return static_cast<char>(std::tolower(c)); };
            std::transform(h.begin(), h.end(), h.begin(), lower);
            std::transform(n.begin(), n.end(), n.begin(), lower);
            return h.find(n) != std::string::npos;
        }

        void WrappedTooltip(const char* text)
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    bool SettingsView::Matches(const char* text) const
    {
        return !IsSearching() || ContainsNoCase(text, m_Search);
    }

    void SettingsView::DrawSearchBar(float trailingWidth)
    {
        const float spacing = trailingWidth > 0.0f ? ImGui::GetStyle().ItemSpacing.x : 0.0f;
        ImGui::SetNextItemWidth(-(trailingWidth + spacing));
        ImGui::InputTextWithHint("##SettingsSearch", ICON_LC_SEARCH " Search settings", m_Search, sizeof(m_Search));
        if (ImGui::IsItemHovered() && IsSearching())
            ImGui::SetTooltip("Searching every category");
    }

    void SettingsView::DrawBody()
    {
        ImGui::BeginChild("##Categories", ImVec2(ImGui::GetFontSize() * 11.0f, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        for (size_t i = 0; i < m_Categories.size(); i++)
        {
            const Category& category = m_Categories[i];
            const std::string label = category.icon.empty() ? category.name : category.icon + "  " + category.name;

            ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
            if (ImGui::Selectable(label.c_str(), !IsSearching() && m_Selected == i, 0, ImVec2(0, ImGui::GetFrameHeight())))
            {
                m_Selected = i;
                m_Search[0] = '\0';
            }
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##Settings", ImVec2(0, 0), ImGuiChildFlags_Borders);
        m_RowsDrawn = 0;
        if (IsSearching())
        {
            for (size_t i = 0; i < m_Categories.size(); i++)
                DrawCategory(i);

            if (m_RowsDrawn == 0)
                ImGui::TextDisabled("No setting matches \"%s\".", m_Search);
        }
        else if (m_Selected < m_Categories.size())
        {
            DrawCategory(m_Selected);
        }
        ImGui::EndChild();
    }

    void SettingsView::DrawCategory(size_t index)
    {
        m_PendingCategory = m_Categories[index].name.c_str();
        m_CurrentCategory = m_PendingCategory;
        m_PendingSection = nullptr;
        m_CurrentSection = nullptr;

        ImGui::PushID(static_cast<int>(index));
        if (m_Categories[index].draw)
            m_Categories[index].draw();
        ImGui::PopID();

        m_PendingCategory = nullptr;
        m_PendingSection = nullptr;
    }

    void SettingsView::Section(const char* title)
    {
        m_PendingSection = title;
        m_CurrentSection = title;
    }

    void SettingsView::FlushPendingHeaders()
    {
        if (m_PendingCategory)
        {
            if (m_RowsDrawn > 0)
            {
                ImGui::Spacing();
                ImGui::Spacing();
            }
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
            ImGui::TextUnformatted(m_PendingCategory);
            ImGui::PopFont();
            m_PendingCategory = nullptr;
        }

        if (m_PendingSection)
        {
            ImGui::SeparatorText(m_PendingSection);
            m_PendingSection = nullptr;
        }
    }

    bool SettingsView::GroupMatches() const
    {
        // Searching for a section or a category shows all of it
        return ContainsNoCase(m_CurrentSection, m_Search) || ContainsNoCase(m_CurrentCategory, m_Search);
    }

    bool SettingsView::BeginRow(const char* label, const char* tooltip, bool modified, bool* reset)
    {
        if (reset)
            *reset = false;

        if (!Matches(label) && !GroupMatches())
            return false;

        FlushPendingHeaders();
        m_RowsDrawn++;

        ImGui::PushID(label);

        const float startX = ImGui::GetCursorPosX();
        const float labelWidth = std::max(ImGui::GetContentRegionAvail().x * 0.4f, ImGui::GetFontSize() * 8.0f);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            WrappedTooltip(tooltip);

        if (modified)
        {
            // Unreal's yellow arrow : the value is not the default any more, one click puts it back
            const float buttonSize = ImGui::GetFrameHeight();
            ImGui::SameLine(startX + labelWidth - buttonSize - ImGui::GetStyle().ItemSpacing.x);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.30f, 1.0f));
            if (ImGui::Button(ICON_LC_ROTATE_CCW "##Reset", ImVec2(buttonSize, buttonSize)) && reset)
                *reset = true;
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reset to default");
        }

        ImGui::SameLine(startX + labelWidth);
        ImGui::SetNextItemWidth(-FLT_MIN);

        ImGui::PopID();
        return true;
    }

    bool SettingsView::BeginBlock(const char* keywords)
    {
        if (IsSearching() && !Matches(keywords) && !GroupMatches())
            return false;

        FlushPendingHeaders();
        m_RowsDrawn++;
        return true;
    }

    bool SettingsView::Checkbox(const char* label, const char* tooltip, bool& value, bool defaultValue)
    {
        bool reset = false;
        if (!BeginRow(label, tooltip, value != defaultValue, &reset))
            return false;
        if (reset)
            value = defaultValue;

        const std::string id = std::string("##") + label;
        return ImGui::Checkbox(id.c_str(), &value) || reset;
    }

    bool SettingsView::DragFloat(const char* label, const char* tooltip, float& value, float defaultValue,
                                 float speed, float min, float max, const char* format)
    {
        bool reset = false;
        if (!BeginRow(label, tooltip, value != defaultValue, &reset))
            return false;
        if (reset)
            value = defaultValue;

        const std::string id = std::string("##") + label;
        return ImGui::DragFloat(id.c_str(), &value, speed, min, max, format, ImGuiSliderFlags_AlwaysClamp) || reset;
    }

    bool SettingsView::SliderFloat(const char* label, const char* tooltip, float& value, float defaultValue,
                                   float min, float max, const char* format)
    {
        bool reset = false;
        if (!BeginRow(label, tooltip, value != defaultValue, &reset))
            return false;
        if (reset)
            value = defaultValue;

        const std::string id = std::string("##") + label;
        return ImGui::SliderFloat(id.c_str(), &value, min, max, format, ImGuiSliderFlags_AlwaysClamp) || reset;
    }

    bool SettingsView::DragInt(const char* label, const char* tooltip, int& value, int defaultValue,
                               float speed, int min, int max, const char* format)
    {
        bool reset = false;
        if (!BeginRow(label, tooltip, value != defaultValue, &reset))
            return false;
        if (reset)
            value = defaultValue;

        const std::string id = std::string("##") + label;
        return ImGui::DragInt(id.c_str(), &value, speed, min, max, format, ImGuiSliderFlags_AlwaysClamp) || reset;
    }

    bool SettingsView::Combo(const char* label, const char* tooltip, int& value, int defaultValue, const char* const items[], int count)
    {
        bool reset = false;
        if (!BeginRow(label, tooltip, value != defaultValue, &reset))
            return false;
        if (reset)
            value = defaultValue;

        const std::string id = std::string("##") + label;
        return ImGui::Combo(id.c_str(), &value, items, count) || reset;
    }
}

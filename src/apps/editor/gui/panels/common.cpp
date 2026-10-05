#include "common.hpp"

#include <vector>

void TextEllipsis(const char *text)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImGuiStyle& style = g.Style;

    ImVec2 pos = window->DC.CursorPos;
    float width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetTextLineHeight();

    ImGui::ItemSize(ImVec2(width, height));
    ImGui::ItemAdd(
        ImRect(
            pos,
            ImVec2(pos.x + width, pos.y + height)
        ),
        0
    );

    ImVec2 pos_max(pos.x + width, pos.y + height);

    ImGui::RenderTextEllipsis(
        window->DrawList,
        pos,
        pos_max,
        pos.x + width, // ellipsis_max_x
        text,
        nullptr,
        nullptr
    );
}

void TextEllipsisCentered(const char* text, float parentWidth)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImGuiStyle& style = g.Style;

    ImVec2 pos = window->DC.CursorPos;
    float width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetTextLineHeight();

    // Ensure we have the full parentWidth for centering the text.
    float availableWidth = parentWidth > 0.0f ? parentWidth : width;

    ImGui::ItemSize(ImVec2(availableWidth, height));
    ImGui::ItemAdd(
        ImRect(
            pos,
            ImVec2(pos.x + availableWidth, pos.y + height)
        ),
        0
    );

    // Calculate the starting position of the text to center it
    float textWidth = ImGui::CalcTextSize(text).x;
    float startX = pos.x + (availableWidth - textWidth) * 0.5f;  // Centered horizontally

    // Update pos with the centered position
    pos.x = startX;

    ImVec2 pos_max(pos.x + textWidth, pos.y + height);

    // Render the text with ellipsis support
    ImGui::RenderTextEllipsis(
        window->DrawList,
        pos,
        pos_max,
        pos.x + availableWidth, // ellipsis_max_x
        text,
        nullptr,
        nullptr
    );
}
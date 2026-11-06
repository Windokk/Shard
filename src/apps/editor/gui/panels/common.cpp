#include "common.hpp"

#include <vector>

void TextEllipsis(const char *text)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

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
        pos.x + width,
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

    ImVec2 pos = window->DC.CursorPos;
    float width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetTextLineHeight();

    float availableWidth = parentWidth > 0.0f ? parentWidth : width;

    ImGui::ItemSize(ImVec2(availableWidth, height));
    ImGui::ItemAdd(
        ImRect(
            pos,
            ImVec2(pos.x + availableWidth, pos.y + height)
        ),
        0
    );

    float textWidth = ImGui::CalcTextSize(text).x;
    float startX = pos.x + (availableWidth - textWidth) * 0.5f;

    pos.x = startX;

    ImVec2 pos_max(pos.x + textWidth, pos.y + height);

    ImGui::RenderTextEllipsis(
        window->DrawList,
        pos,
        pos_max,
        pos.x + availableWidth,
        text,
        nullptr,
        nullptr
    );
}
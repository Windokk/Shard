#include "profiler_panel.hpp"

#include "engine/core/engine.hpp"

#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace Shard::Editor::GUI{

    using Shard::Engine::Debugging::ProfileCategory;
    using Shard::Engine::Debugging::FrameProfile;
    using Shard::Engine::Debugging::Profiler;
    using Shard::Engine::Debugging::kProfileCategoryCount;

    static ImU32 CategoryColor(ProfileCategory category)
    {
        switch(category){
            case ProfileCategory::Physics:      return IM_COL32(231, 76, 60, 255);
            case ProfileCategory::Audio:        return IM_COL32(155, 89, 182, 255);
            case ProfileCategory::Scripting:    return IM_COL32(46, 204, 113, 255);
            case ProfileCategory::Rendering:    return IM_COL32(52, 152, 219, 255);
            case ProfileCategory::Input:        return IM_COL32(241, 196, 15, 255);
            case ProfileCategory::Presentation: return IM_COL32(230, 126, 34, 255);
            case ProfileCategory::Other:        return IM_COL32(127, 140, 141, 255);
            default:                            return IM_COL32(255, 255, 255, 255);
        }
    }

    void ProfilerPanel::Draw()
    {
        ImGui::Begin("Profiler");

        Profiler* profiler = Engine::Core::GetEngine().GetProfiler();

        timeSinceRefresh += ImGui::GetIO().DeltaTime;
        if (timeSinceRefresh >= refreshIntervalSeconds)
        {
            displayedFrame = profiler->GetLastFrameProfile();
            timeSinceRefresh = 0.0f;
        }

        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderFloat("Refresh interval", &refreshIntervalSeconds, 0.05f, 2.0f, "%.2f s");

        float fps = displayedFrame.totalMs > 0.0f ? 1000.0f / displayedFrame.totalMs : 0.0f;
        ImGui::Text("Frame: %.2f ms (%.1f FPS)", displayedFrame.totalMs, fps);

        ImGui::Separator();
        ImGui::TextUnformatted("Breakdown (last frame)");
        ImGui::TextDisabled("Click a category to expand its sub-counters");
        DrawBreakdown(displayedFrame);
        ImGui::Separator();
        ImGui::TextUnformatted("History");
        DrawHistory(*profiler);

        ImGui::End();
    }

    void ProfilerPanel::DrawBreakdown(const FrameProfile& frame)
    {
        struct Entry { ProfileCategory category; float ms; };

        std::array<Entry, kProfileCategoryCount> entries{};
        for (size_t i = 0; i < kProfileCategoryCount; i++)
            entries[i] = { static_cast<ProfileCategory>(i), frame.categoryMs[i] };

        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b){
            return a.ms > b.ms;
        });

        // Single bar showing how the frame is split between subsystems, in descending order.
        ImVec2 barPos = ImGui::GetCursorScreenPos();
        ImVec2 barSize = ImVec2(ImGui::GetContentRegionAvail().x, 20.0f);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(barPos, ImVec2(barPos.x + barSize.x, barPos.y + barSize.y), IM_COL32(30, 30, 30, 255));

        float x = barPos.x;
        for (const Entry& entry : entries)
        {
            if (frame.totalMs <= 0.0f || entry.ms <= 0.0f)
                continue;

            float w = (entry.ms / frame.totalMs) * barSize.x;
            draw->AddRectFilled(ImVec2(x, barPos.y), ImVec2(x + w, barPos.y + barSize.y), CategoryColor(entry.category));
            x += w;
        }
        ImGui::Dummy(barSize);

        ImGui::Spacing();

        for (size_t i = 0; i < entries.size(); i++)
        {
            const Entry& entry = entries[i];
            float pct = frame.totalMs > 0.0f ? (entry.ms / frame.totalMs) * 100.0f : 0.0f;
            int categoryIndex = static_cast<int>(entry.category);
            bool isSelected = (selectedCategory == categoryIndex);

            ImGui::PushID(categoryIndex);

            ImVec4 swatch = ImGui::ColorConvertU32ToFloat4(CategoryColor(entry.category));
            ImGui::ColorButton("##swatch", swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(12, 12));
            ImGui::SameLine();

            // Highlight the biggest consumer this frame.
            bool isTop = (i == 0 && entry.ms > 0.0f);
            if (isTop)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));

            char label[128];
            snprintf(label, sizeof(label), "%-12s %6.2f ms (%4.1f%%)", Engine::Debugging::ProfileCategoryName(entry.category), entry.ms, pct);

            // Clicking a category toggles its selection, expanding/collapsing its sub-counters below.
            if (ImGui::Selectable(label, isSelected))
                selectedCategory = isSelected ? -1 : categoryIndex;

            if (isTop)
                ImGui::PopStyleColor();

            // Sub-measures nested within this category (e.g. glDrawElements under Rendering),
            // only shown while this category is the one selected.
            if (isSelected && entry.category == ProfileCategory::Rendering)
            {
                float renderingMs = frame.categoryMs[static_cast<size_t>(ProfileCategory::Rendering)];

                ImGui::Indent(20.0f);
                for (size_t s = 0; s < Engine::Debugging::kRenderSubSampleCount; s++)
                {
                    auto sample = static_cast<Engine::Debugging::RenderSubSample>(s);
                    float subMs = frame.renderSubMs[s];
                    float subPct = renderingMs > 0.0f ? (subMs / renderingMs) * 100.0f : 0.0f;

                    ImGui::TextDisabled("%-18s %6.2f ms (%4.1f%% of Rendering)", Engine::Debugging::RenderSubSampleName(sample), subMs, subPct);
                }
                ImGui::Unindent(20.0f);
            }

            ImGui::PopID();
        }
    }

    void ProfilerPanel::DrawHistory(const Profiler& profiler)
    {
        const auto& history = profiler.GetProfileHistory();
        size_t count = profiler.GetProfileHistoryCount();
        size_t cursor = profiler.GetProfileHistoryCursor();

        if (count == 0)
        {
            ImGui::TextDisabled("Not enough frames yet.");
            return;
        }

        // 60 FPS frame budget kept as a floor so the graph doesn't over-zoom on a few cheap frames.
        float maxMs = 16.6f;
        for (size_t i = 0; i < count; i++)
            maxMs = std::max(maxMs, history[i].totalMs);
        maxMs *= 1.1f;

        ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, 120.0f);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + size.x, p0.y + size.y);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(p0, p1, IM_COL32(20, 20, 20, 255));

        float colWidth = size.x / static_cast<float>(Profiler::kHistoryLength);

        for (size_t i = 0; i < count; i++)
        {
            size_t idx = (cursor + Profiler::kHistoryLength - count + i) % Profiler::kHistoryLength;
            const FrameProfile& frame = history[idx];

            float x0 = p0.x + i * colWidth;
            float x1 = x0 + colWidth + 1.0f;

            float yBase = p1.y;
            for (size_t c = 0; c < kProfileCategoryCount; c++)
            {
                float h = (frame.categoryMs[c] / maxMs) * size.y;
                if (h <= 0.0f)
                    continue;

                float yTop = yBase - h;
                draw->AddRectFilled(ImVec2(x0, yTop), ImVec2(x1, yBase), CategoryColor(static_cast<ProfileCategory>(c)));
                yBase = yTop;
            }
        }

        // 60 FPS reference line.
        float y60 = p1.y - (16.6f / maxMs) * size.y;
        draw->AddLine(ImVec2(p0.x, y60), ImVec2(p1.x, y60), IM_COL32(255, 255, 255, 130), 3.0f);

        ImGui::Dummy(size);
        ImGui::TextDisabled("White line = 16.6 ms (60 FPS budget)");
    }
}

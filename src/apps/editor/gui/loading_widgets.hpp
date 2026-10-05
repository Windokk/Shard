#pragma once

// Small shared drawing helpers for "something is loading" UI - used by both the full-screen level
// loading overlay (main_window.cpp) and the DDGI probe-bake progress toast (ImGuiNotify.hpp), so the
// two don't drift into two different-looking loading widgets. Header-only (inline), matching
// ImGuiNotify.hpp's own style, so either caller can use it without a new translation unit to wire into
// CMake's file(GLOB_RECURSE ...) source list.

#include <cmath>
#include <algorithm>

#include "imgui.h"

namespace Shard::Editor::GUI::LoadingWidgets
{
    // Rotating, breathing arc - draws an open circle whose arc length eases between minArc and maxArc
    // turns of the circle while the whole thing spins, the standard "indeterminate activity" spinner
    // look. Used in place of a static icon glyph, which reads as frozen/stuck the instant a load takes
    // more than a second.
    inline void DrawSpinner(ImDrawList* drawList, ImVec2 centre, float radius, float thickness, ImU32 color)
    {
        constexpr float kSpinSpeed = 2.6f;   // radians/sec the arc's leading edge rotates at
        constexpr float kPulseSpeed = 1.8f;  // rad/sec of the arc-length breathing cycle
        constexpr float kMinArc = 0.12f;     // shortest arc, in full turns
        constexpr float kMaxArc = 0.75f;     // longest arc, in full turns
        constexpr int kSegments = 32;

        float time = (float)ImGui::GetTime();
        float pulse = (std::sin(time * kPulseSpeed) + 1.0f) * 0.5f;
        float arcTurns = kMinArc + (kMaxArc - kMinArc) * pulse;

        float angleStart = time * kSpinSpeed;
        float angleEnd = angleStart + arcTurns * 2.0f * IM_PI;

        drawList->PathClear();
        for (int i = 0; i <= kSegments; i++)
        {
            float t = (float)i / (float)kSegments;
            float angle = angleStart + t * (angleEnd - angleStart);
            drawList->PathLineTo(ImVec2(centre.x + std::cos(angle) * radius, centre.y + std::sin(angle) * radius));
        }
        drawList->PathStroke(color, ImDrawFlags_None, thickness);
    }

    // Rounded, filled bar with a thin border - ImGui::ProgressBar() draws a flat, square-cornered
    // rectangle with no room for either, which is what made the old loading bars look like a
    // placeholder rather than a finished piece of UI.
    inline void DrawProgressBar(ImDrawList* drawList, ImVec2 pos, ImVec2 size, float fraction,
        ImU32 bgColor, ImU32 fillColor, ImU32 borderColor)
    {
        fraction = std::clamp(fraction, 0.0f, 1.0f);
        float rounding = size.y * 0.5f;
        ImVec2 max(pos.x + size.x, pos.y + size.y);

        drawList->AddRectFilled(pos, max, bgColor, rounding);

        float fillWidth = size.x * fraction;
        if (fillWidth > 1.0f)
        {
            ImVec2 fillMax(pos.x + fillWidth, max.y);
            // Only round the right edge once the fill has grown past the left edge's own rounding,
            // otherwise a freshly-started bar draws a rounded cap floating past its actual fill edge.
            ImDrawFlags fillRounding = fillWidth >= size.y ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft;
            drawList->AddRectFilled(pos, fillMax, fillColor, rounding, fillRounding);
        }

        drawList->AddRect(pos, max, borderColor, rounding, ImDrawFlags_RoundCornersAll, 1.5f);
    }

    /*
    * The MIT License (MIT)
    *
    * Copyright (c) 2021-2022 Dalerank
    *
    * Permission is hereby granted, free of charge, to any person obtaining a copy
    * of this software and associated documentation files (the "Software"), to deal
    * in the Software without restriction, including without limitation the rights
    * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    * copies of the Software, and to permit persons to whom the Software is
    * furnished to do so, subject to the following conditions:
    *
    * The above copyright notice and this permission notice shall be included in all
    * copie or substantial portions of the Software.
    * 
    * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    * SOFTWARE.
    * 
    */
    inline bool SpinnerBegin(const char *label, float radius, ImVec2 &pos, ImVec2 &size, ImVec2 &centre, int &num_segments) {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
          return false;

        ImGuiContext &g = *GImGui;
        const ImGuiStyle &style = g.Style;
        const ImGuiID id = window->GetID(label);

        pos = window->DC.CursorPos;
        // The size of the spinner is set to twice the radius, plus some padding based on the style
        size = ImVec2((radius) * 2, (radius + style.FramePadding.y) * 2);

        const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        ImGui::ItemSize(bb, style.FramePadding.y);

        num_segments = window->DrawList->_CalcCircleAutoSegmentCount(radius);

        centre = bb.GetCenter();
        // If the item cannot be added to the window, return false
        if (!ImGui::ItemAdd(bb, id))
          return false;

        return true;
    }

    inline void PathStroke(ImDrawList* draw_list, const ImU32 col, const float thickness, const ImDrawFlags flags) {
    #if IMGUI_VERSION_NUM >= 19276
            draw_list->PathStroke(col, thickness, flags);
    #else
            draw_list->PathStroke(col, flags, thickness);
    #endif
    }

    constexpr float PI_DIV_4 = IM_PI / 4.f;
    constexpr float PI_DIV_2 = IM_PI / 2.f;
    constexpr float PI_2 = IM_PI * 2.f;
    template<class T> constexpr float PI_DIV(T d) { return IM_PI / (float)d; }
    template<class T> constexpr float PI_2_DIV(T d) { return PI_2 / (float)d; }
    
    inline ImColor color_alpha(ImColor c, float alpha) { c.Value.w *= alpha * ImGui::GetStyle().Alpha; return c; }
    
    enum ease_mode {
        e_ease_none = 0,
        e_ease_inoutquad = 1,
        e_ease_inoutexpo = 2,
        e_ease_spring = 3,
        e_ease_gravity = 4,
        e_ease_infinity = 5,
        e_ease_elastic = 6,
        e_ease_sine = 7,
        e_ease_damping = 8,
    };

    template<typename ... Args>
    inline float ease(ease_mode mode, Args ... args) {
        static_assert((std::is_same_v<Args, float> && ...), "All arguments should be of type float");
        float params[] = {args...};
        switch (mode) {
        case e_ease_inoutquad: return ease_inoutquad(params);
        case e_ease_inoutexpo: return ease_inoutexpo(params);
        case e_ease_spring: return ease_spring(params);
        case e_ease_gravity: return ease_gravity(params);
        case e_ease_infinity: return ease_infinity(params);
        case e_ease_elastic: return ease_inoutelastic(params);
        case e_ease_sine: return ease_sine(params);
        case e_ease_damping: return ease_damping(params);
        case e_ease_none: return (0.f);
        }
        return 0.f;
    }

    // Concentric filled rings that pulse outward and fade - a solid-circle alternative to DrawSpinner's
    // outlined arc. Takes an explicit draw list and centre position, like DrawSpinner above, rather than
    // reserving its own ImGui layout item, so callers can place (and spin) it anywhere.
    inline void SpinnerFadePulsar(ImDrawList* drawList, ImVec2 centre, float radius, const ImColor &color = ImColor(1, 1, 1, 1), float speed = 2.8f, int rings = 2)
    {
      constexpr int kSegments = 32;
      const float koeff = PI_DIV(2 * rings);
      float start = (float)ImGui::GetTime() * speed;

      for (int num_ring = 0; num_ring < rings; ++num_ring) {
        float radius_k = std::sin(std::fmod(start + (num_ring * koeff), PI_DIV_2));
        ImColor c = color_alpha(color, (radius_k > 0.5f) ? (2.f - (radius_k * 2.f)) : color.Value.w);
        drawList->AddCircleFilled(centre, radius_k * radius, c, kSegments);
      }
    }
}

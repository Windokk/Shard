#include "asset_icon.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <initializer_list>

namespace Shard::Editor::GUI {

    namespace {

        using Type = Engine::Filesystem::Type;

        ImU32 WithAlpha(ImU32 color, float alpha)
        {
            ImVec4 v = ImGui::ColorConvertU32ToFloat4(color);
            v.w = alpha;
            return ImGui::ColorConvertFloat4ToU32(v);
        }

        // Flat, solid shapes on a 24 x 24 grid : the accent at full strength for what matters, at `kSoft` for the rest.
        // Square corners, no outlines, nothing thinner than a bar.
        constexpr float kSoft = 0.5f;

        struct Canvas {
            ImDrawList* list;
            ImVec2 origin;
            float scale;
            ImU32 color;

            ImVec2 P(float x, float y) const { return ImVec2(origin.x + x * scale, origin.y + y * scale); }
            ImU32 Tone(float alpha) const { return alpha >= 1.0f ? color : WithAlpha(color, alpha); }

            void Rect(float x0, float y0, float x1, float y1, float alpha = 1.0f) const
            {
                list->AddRectFilled(P(x0, y0), P(x1, y1), Tone(alpha));
            }

            void Poly(std::initializer_list<ImVec2> points, float alpha = 1.0f) const
            {
                ImVec2 mapped[8];
                int n = 0;
                for (const ImVec2& p : points)
                    if (n < 8)
                        mapped[n++] = P(p.x, p.y);
                list->AddConvexPolyFilled(mapped, n, Tone(alpha));
            }

            void Disc(float x, float y, float r, float alpha = 1.0f) const
            {
                list->AddCircleFilled(P(x, y), r * scale, Tone(alpha), 32);
            }

            void Stroke(std::initializer_list<ImVec2> points, float width) const
            {
                ImVec2 mapped[8];
                int n = 0;
                for (const ImVec2& p : points)
                    if (n < 8)
                        mapped[n++] = P(p.x, p.y);
                list->AddPolyline(mapped, n, color, ImDrawFlags_None, width * scale);
            }
        };

        void Glyph(const Canvas& c, Type type)
        {
            switch (type)
            {
                case Type::T_WORLD:             // two peaks and a sun
                    c.Disc(17.5f, 7.0f, 2.5f);
                    c.Poly({{2, 20}, {9, 8}, {16, 20}}, kSoft);
                    c.Poly({{10, 20}, {16, 11}, {22, 20}});
                    break;
                case Type::T_MODEL:             // cube, three lit faces
                    c.Poly({{12, 2.5f}, {21, 7.5f}, {12, 12.5f}, {3, 7.5f}});
                    c.Poly({{3, 7.5f}, {12, 12.5f}, {12, 22}, {3, 16.5f}}, 0.65f);
                    c.Poly({{21, 7.5f}, {12, 12.5f}, {12, 22}, {21, 16.5f}}, 0.35f);
                    break;
                case Type::T_MATERIAL:          // lit sphere
                    c.Disc(12, 12, 9.5f, kSoft);
                    c.Disc(10.5f, 10.5f, 6.5f);
                    break;
                case Type::T_IMAGE:             // checker : the texture
                    c.Rect(3, 3, 21, 21, kSoft);
                    c.Rect(3, 3, 12, 12);
                    c.Rect(12, 12, 21, 21);
                    break;
                case Type::T_SOUND:             // level bars
                    c.Rect(3, 9, 6, 15);
                    c.Rect(7.5f, 4, 10.5f, 20);
                    c.Rect(12, 7, 15, 17);
                    c.Rect(16.5f, 2.5f, 19.5f, 21.5f);
                    c.Rect(21, 10, 22.5f, 14, kSoft);
                    break;
                case Type::T_SCRIPT:            // </>
                    c.Stroke({{8.5f, 6}, {2.5f, 12}, {8.5f, 18}}, 2.6f);
                    c.Stroke({{15.5f, 6}, {21.5f, 12}, {15.5f, 18}}, 2.6f);
                    c.Poly({{13.2f, 3.5f}, {15.4f, 4.2f}, {10.8f, 20.5f}, {8.6f, 19.8f}}, kSoft);
                    break;
                case Type::T_SHADER:            // prism : a lit half and a dark half
                    c.Poly({{12, 2.5f}, {21.5f, 20}, {2.5f, 20}}, kSoft);
                    c.Poly({{12, 2.5f}, {12, 20}, {2.5f, 20}});
                    break;
                case Type::T_COMPUTE_SHADER:    // chip
                    c.Rect(6, 6, 18, 18);
                    for (float p : {8.5f, 14.0f})
                    {
                        c.Rect(p, 2.5f, p + 1.5f, 6, kSoft);
                        c.Rect(p, 18, p + 1.5f, 21.5f, kSoft);
                        c.Rect(2.5f, p, 6, p + 1.5f, kSoft);
                        c.Rect(18, p, 21.5f, p + 1.5f, kSoft);
                    }
                    break;
                case Type::T_PROBES:            // grid of probes : lines, a point on every crossing
                    for (float p : {4.0f, 12.0f, 20.0f})
                    {
                        c.Rect(p - 0.7f, 4, p + 0.7f, 20, kSoft);
                        c.Rect(4, p - 0.7f, 20, p + 0.7f, kSoft);
                    }
                    for (float y : {4.0f, 12.0f, 20.0f})
                        for (float x : {4.0f, 12.0f, 20.0f})
                            c.Disc(x, y, 2.3f);
                    break;
                case Type::T_LIGHTMAP:          // atlas : cells lit by different amounts
                    c.Rect(3, 3, 11.5f, 11.5f);
                    c.Rect(12.5f, 3, 21, 11.5f, 0.65f);
                    c.Rect(3, 12.5f, 11.5f, 21, 0.4f);
                    c.Rect(12.5f, 12.5f, 21, 21, 0.2f);
                    break;
                case Type::T_FONT:              // T
                    c.Rect(3.5f, 3.5f, 20.5f, 8);
                    c.Rect(10, 8, 14, 20.5f);
                    c.Rect(7.5f, 18, 16.5f, 20.5f, kSoft);
                    break;
                case Type::T_CONFIG:            // sliders
                    c.Rect(3, 5.5f, 21, 7.5f, kSoft);
                    c.Rect(3, 11, 21, 13, kSoft);
                    c.Rect(3, 16.5f, 21, 18.5f, kSoft);
                    c.Rect(13, 3.5f, 17, 9.5f);
                    c.Rect(6, 9, 10, 15);
                    c.Rect(14, 14.5f, 18, 20.5f);
                    break;
                case Type::T_TEXT:              // lines
                default:
                    c.Rect(3.5f, 4.5f, 20.5f, 7);
                    c.Rect(3.5f, 10.75f, 20.5f, 13.25f);
                    c.Rect(3.5f, 17, 14, 19.5f);
                    break;
            }
        }

        bool WantsBadge(Type type)
        {
            switch (type)
            {
                case Type::T_MODEL: case Type::T_IMAGE: case Type::T_SOUND: case Type::T_SCRIPT: case Type::T_SHADER:
                case Type::T_COMPUTE_SHADER: case Type::T_FONT: case Type::T_CONFIG: case Type::T_TEXT:
                    return true;
                default:
                    return false;
            }
        }
    }

    void DrawAssetIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max, Type type, bool isDirectory, ImU32 accent, const char* extension)
    {
        const float size = std::min(max.x - min.x, max.y - min.y);
        if (size < 4.0f)
            return;
        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);

        // The glyph fills the whole square : no tile behind it, no padding
        const Canvas c{drawList, ImVec2(center.x - size * 0.5f, center.y - size * 0.5f), size / 24.0f, accent};

        if (isDirectory)
        {
            // The back panel with its tab, the front panel over it, one left edge
            c.Poly({{3, 4.5f}, {9.5f, 4.5f}, {11.5f, 7}, {3, 7}}, 0.55f);
            c.Rect(3, 7, 21, 19.5f, 0.55f);
            c.Rect(3, 10, 21, 19.5f);
            return;
        }

        Glyph(c, type);

        if (extension && extension[0] && WantsBadge(type) && size >= 44.0f)
        {
            char text[8];
            size_t n = 0;
            for (const char* p = extension; *p && n < 4; ++p)
                if (*p != '.')
                    text[n++] = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
            text[n] = 0;
            if (n == 0)
                return;

            ImFont* font = ImGui::GetFont();
            const float fontSize = std::max(9.0f, size * 0.15f);
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, 1000.0f, 0.0f, text);
            const ImVec2 pad(3.0f, 1.0f);
            const ImVec2 b1(max.x - 4.0f, max.y - 4.0f);
            const ImVec2 b0(b1.x - textSize.x - pad.x * 2.0f, b1.y - textSize.y - pad.y * 2.0f);
            drawList->AddRectFilled(b0, b1, IM_COL32(24, 24, 28, 230));
            drawList->AddText(font, fontSize, ImVec2(b0.x + pad.x, b0.y + pad.y), WithAlpha(accent, 0.95f), text);
        }
    }
}

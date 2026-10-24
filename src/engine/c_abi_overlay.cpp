#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <shard/shard_debug.h>
#include <shard/shard_render.h>

#include "engine/c_abi_render_internal.hpp"
#include "engine/core/math/large_world.hpp"
#include "engine/platform/windowing/iwindow.hpp"
#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/time_manager.hpp"

// shard.render's 2D overlay (the plugin's callback records geometry, scissors and clears into the frame's snapshot) and shard.debug (shapes in the
// world, projected on the CPU and drawn through that same overlay).

namespace Shard::Engine::Plugins::CAbi {

    using namespace Rendering;

    void HookRenderRelease(PluginContext& ctx, PluginOwner owner);

    namespace {
        struct OverlayRec {
            PluginOwner owner = 0;
            std::string name;
            int32_t order = 0;
            uint32_t flags = 0;
            ShardTexture target = 0;
            ShardOverlayFn draw = nullptr;
            void* user = nullptr;
        };

        OwnedTable<OverlayRec> gOverlays;
        OwnedTable<OverlayGeometry> gGeometries;
        float gViewportOrigin[2] = { 0.0f, 0.0f };      // where the viewport is in the window (the editor says)

        // What the overlay being asked to draw is recording into
        struct Recording {
            OverlayProxy* proxy = nullptr;
            PluginOwner owner = 0;
        };
        thread_local Recording* tRecording = nullptr;

        std::shared_ptr<Texture2D> TextureOf(ShardTexture handle)
        {
            return tRecording && handle ? ResolveTexture(tRecording->owner, handle) : nullptr;
        }

        void SHARD_CALL DrawGeometry(ShardGeometry geometry, ShardTexture texture, const float translation[2])
        {
            if (!tRecording) return;
            std::shared_ptr<OverlayGeometry> g = gGeometries.Get(tRecording->owner, geometry);
            if (!g) return;
            OverlayCommand c;
            c.kind = OverlayCommand::Kind::Geometry;
            c.geometry = std::move(g);
            c.texture = TextureOf(texture);
            if (translation) c.translation = glm::vec2(translation[0], translation[1]);
            tRecording->proxy->commands.push_back(std::move(c));
        }

        void SHARD_CALL SetTransform(const float matrix[16])
        {
            if (!tRecording) return;
            OverlayCommand c;
            c.kind = OverlayCommand::Kind::Transform;
            if (matrix) std::memcpy(&c.transform[0][0], matrix, sizeof(float) * 16);
            tRecording->proxy->commands.push_back(std::move(c));
        }

        void SHARD_CALL SetScissor(int32_t enabled, int32_t x, int32_t y, int32_t width, int32_t height)
        {
            if (!tRecording) return;
            OverlayCommand c;
            c.kind = OverlayCommand::Kind::Scissor;
            c.enabled = enabled != 0;
            c.scissor[0] = x; c.scissor[1] = y; c.scissor[2] = std::max(width, 0); c.scissor[3] = std::max(height, 0);
            tRecording->proxy->commands.push_back(std::move(c));
        }

        void SHARD_CALL SetClipMask(uint32_t operation, ShardGeometry geometry, const float translation[2])
        {
            if (!tRecording) return;
            OverlayCommand c;
            c.kind = OverlayCommand::Kind::ClipMask;
            c.operation = operation;
            c.geometry = gGeometries.Get(tRecording->owner, geometry);
            if (translation) c.translation = glm::vec2(translation[0], translation[1]);
            tRecording->proxy->commands.push_back(std::move(c));
        }

        void SHARD_CALL Clear(const float rgba[4])
        {
            if (!tRecording || !rgba) return;
            OverlayCommand c;
            c.kind = OverlayCommand::Kind::Clear;
            c.color = glm::vec4(rgba[0], rgba[1], rgba[2], rgba[3]);
            tRecording->proxy->commands.push_back(std::move(c));
        }

        void SHARD_CALL DrawTriangles(const ShardVertex2D* vertices, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount, ShardTexture texture)
        {
            if (!tRecording || !vertices || !indices || vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0) return;
            OverlayProxy& proxy = *tRecording->proxy;
            const uint32_t base = uint32_t(proxy.inlineVertices.size());
            for (uint32_t i = 0; i < vertexCount; ++i) {
                OverlayVertex v;
                v.x = vertices[i].position[0]; v.y = vertices[i].position[1]; v.color = vertices[i].color; v.u = vertices[i].uv[0]; v.v = vertices[i].uv[1];
                proxy.inlineVertices.push_back(v);
            }
            OverlayCommand c;
            c.kind = OverlayCommand::Kind::Triangles;
            c.first = uint32_t(proxy.inlineIndices.size());
            c.count = indexCount;
            for (uint32_t i = 0; i < indexCount; ++i) proxy.inlineIndices.push_back(base + std::min(indices[i], vertexCount - 1));
            c.texture = TextureOf(texture);
            proxy.commands.push_back(std::move(c));
        }

        const ShardDrawApi kDraw = { sizeof(ShardDrawApi), &DrawGeometry, &SetTransform, &SetScissor, &SetClipMask, &Clear, &DrawTriangles };

        // ---- shard.debug : the shapes, as lines and triangles in the world ----

        struct DebugLine { glm::vec3 a, b; uint32_t color; float remaining; };
        struct DebugTriangle { glm::vec3 a, b, c; uint32_t color; float remaining; };
        struct DebugText { glm::vec3 position; bool screen; std::string text; uint32_t color; float remaining; };

        std::mutex gDebugMutex;
        std::vector<DebugLine> gLines;
        std::vector<DebugTriangle> gTriangles;
        std::vector<DebugText> gTexts;
        constexpr size_t kMaxDebugLines = 200000;

        bool DebugOn()
        {
            Core::IEngineContext* engine = Core::gSharedEnginePtr;
            return engine && engine->GetRenderContext() != nullptr;
        }

        glm::vec3 V3(const float v[3]) { return glm::vec3(v[0], v[1], v[2]); }
        bool Finite3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

        void AddLine(const glm::vec3& a, const glm::vec3& b, uint32_t color, float duration)
        {
            if (gLines.size() < kMaxDebugLines) gLines.push_back({ a, b, color, duration });
        }

        // A circle of `radius` around `center` in the plane spanned by u and v
        void AddCircle(const glm::vec3& center, const glm::vec3& u, const glm::vec3& v, float radius, uint32_t color, float duration, int segments = 24)
        {
            glm::vec3 previous = center + u * radius;
            for (int i = 1; i <= segments; ++i) {
                const float angle = 6.28318530718f * float(i) / float(segments);
                const glm::vec3 p = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
                AddLine(previous, p, color, duration);
                previous = p;
            }
        }

        // Two vectors perpendicular to `n` and to each other
        void Basis(const glm::vec3& n, glm::vec3& u, glm::vec3& v)
        {
            const glm::vec3 axis = std::abs(n.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            u = glm::normalize(glm::cross(n, axis));
            v = glm::cross(n, u);
        }

        int32_t SHARD_CALL DbgEnabled() { return DebugOn() ? SHARD_TRUE : SHARD_FALSE; }

        void SHARD_CALL DbgLine(const float from[3], const float to[3], uint32_t color, float duration, uint32_t)
        {
            if (!from || !to || !Finite3(from) || !Finite3(to)) return;
            std::lock_guard<std::mutex> lock(gDebugMutex);
            AddLine(V3(from), V3(to), color, duration);
        }

        void SHARD_CALL DbgLines(const float* points, uint32_t count, uint32_t color, float duration, uint32_t)
        {
            if (!points) return;
            std::lock_guard<std::mutex> lock(gDebugMutex);
            for (uint32_t i = 0; i + 1 < count; i += 2) {
                if (!Finite3(points + i * 3) || !Finite3(points + (i + 1) * 3)) continue;
                AddLine(V3(points + i * 3), V3(points + (i + 1) * 3), color, duration);
            }
        }

        void SHARD_CALL DbgPoint(const float position[3], float size, uint32_t color, float duration, uint32_t)
        {
            if (!position || !Finite3(position)) return;
            const glm::vec3 p = V3(position);
            const float h = std::max(size, 0.001f) * 0.5f;
            std::lock_guard<std::mutex> lock(gDebugMutex);
            AddLine(p - glm::vec3(h, 0, 0), p + glm::vec3(h, 0, 0), color, duration);
            AddLine(p - glm::vec3(0, h, 0), p + glm::vec3(0, h, 0), color, duration);
            AddLine(p - glm::vec3(0, 0, h), p + glm::vec3(0, 0, h), color, duration);
        }

        void SHARD_CALL DbgArrow(const float from[3], const float to[3], uint32_t color, float duration, uint32_t)
        {
            if (!from || !to || !Finite3(from) || !Finite3(to)) return;
            const glm::vec3 a = V3(from), b = V3(to);
            const glm::vec3 d = b - a;
            const float length = glm::length(d);
            std::lock_guard<std::mutex> lock(gDebugMutex);
            AddLine(a, b, color, duration);
            if (length < 1e-6f) return;
            const glm::vec3 dir = d / length;
            glm::vec3 u, v;
            Basis(dir, u, v);
            const float head = std::min(length * 0.25f, 0.5f);
            const glm::vec3 base = b - dir * head;
            AddLine(b, base + u * head * 0.4f, color, duration);
            AddLine(b, base - u * head * 0.4f, color, duration);
            AddLine(b, base + v * head * 0.4f, color, duration);
            AddLine(b, base - v * head * 0.4f, color, duration);
        }

        void SHARD_CALL DbgCircle(const float center[3], const float normal[3], float radius, uint32_t color, float duration, uint32_t)
        {
            if (!center || !normal || !Finite3(center) || !Finite3(normal) || !(radius > 0.0f)) return;
            glm::vec3 n = V3(normal);
            if (glm::dot(n, n) < 1e-12f) return;
            n = glm::normalize(n);
            glm::vec3 u, v;
            Basis(n, u, v);
            std::lock_guard<std::mutex> lock(gDebugMutex);
            AddCircle(V3(center), u, v, radius, color, duration, 32);
        }

        void SHARD_CALL DbgSphere(const float center[3], float radius, uint32_t color, float duration, uint32_t)
        {
            if (!center || !Finite3(center) || !(radius > 0.0f)) return;
            const glm::vec3 c = V3(center);
            std::lock_guard<std::mutex> lock(gDebugMutex);
            AddCircle(c, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), radius, color, duration, 32);
            AddCircle(c, glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), radius, color, duration, 32);
            AddCircle(c, glm::vec3(0, 0, 1), glm::vec3(1, 0, 0), radius, color, duration, 32);
        }

        void SHARD_CALL DbgBox(const float center[3], const float half[3], const float rotation[4], uint32_t color, float duration, uint32_t)
        {
            if (!center || !half || !Finite3(center) || !Finite3(half)) return;
            glm::quat q(1, 0, 0, 0);
            if (rotation && std::isfinite(rotation[0]) && std::isfinite(rotation[1]) && std::isfinite(rotation[2]) && std::isfinite(rotation[3])) {
                const glm::quat r(rotation[3], rotation[0], rotation[1], rotation[2]);
                if (glm::dot(r, r) > 1e-12f) q = glm::normalize(r);
            }
            const glm::vec3 c = V3(center), h = V3(half);
            glm::vec3 corner[8];
            for (int i = 0; i < 8; ++i)
                corner[i] = c + q * glm::vec3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
            static const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
            std::lock_guard<std::mutex> lock(gDebugMutex);
            for (const auto& e : edges) AddLine(corner[e[0]], corner[e[1]], color, duration);
        }

        void SHARD_CALL DbgCapsule(const float from[3], const float to[3], float radius, uint32_t color, float duration, uint32_t)
        {
            if (!from || !to || !Finite3(from) || !Finite3(to) || !(radius > 0.0f)) return;
            const glm::vec3 a = V3(from), b = V3(to);
            glm::vec3 axis = b - a;
            const float length = glm::length(axis);
            axis = length > 1e-6f ? axis / length : glm::vec3(0, 1, 0);
            glm::vec3 u, v;
            Basis(axis, u, v);
            std::lock_guard<std::mutex> lock(gDebugMutex);
            AddCircle(a, u, v, radius, color, duration);
            AddCircle(b, u, v, radius, color, duration);
            AddLine(a + u * radius, b + u * radius, color, duration);
            AddLine(a - u * radius, b - u * radius, color, duration);
            AddLine(a + v * radius, b + v * radius, color, duration);
            AddLine(a - v * radius, b - v * radius, color, duration);
            // The caps : half circles in two planes
            for (int side = 0; side < 2; ++side) {
                const glm::vec3 center = side ? b : a;
                const glm::vec3 pole = side ? axis : -axis;
                for (const glm::vec3& w : { u, v }) {
                    glm::vec3 previous = center + w * radius;
                    for (int i = 1; i <= 8; ++i) {
                        const float angle = 1.57079632679f * float(i) / 8.0f;
                        const glm::vec3 p = center + (w * std::cos(angle) + pole * std::sin(angle)) * radius;
                        AddLine(previous, p, color, duration);
                        previous = p;
                    }
                    previous = center - w * radius;
                    for (int i = 1; i <= 8; ++i) {
                        const float angle = 1.57079632679f * float(i) / 8.0f;
                        const glm::vec3 p = center + (-w * std::cos(angle) + pole * std::sin(angle)) * radius;
                        AddLine(previous, p, color, duration);
                        previous = p;
                    }
                }
            }
        }

        void SHARD_CALL DbgCone(const float apex[3], const float axisIn[3], float halfAngleDegrees, uint32_t color, float duration, uint32_t)
        {
            if (!apex || !axisIn || !Finite3(apex) || !Finite3(axisIn) || !(halfAngleDegrees > 0.0f && halfAngleDegrees < 89.9f)) return;
            const glm::vec3 a = V3(apex), axis = V3(axisIn);
            const float length = glm::length(axis);
            if (length < 1e-6f) return;
            const glm::vec3 dir = axis / length;
            glm::vec3 u, v;
            Basis(dir, u, v);
            const float radius = length * std::tan(glm::radians(halfAngleDegrees));
            const glm::vec3 base = a + axis;
            std::lock_guard<std::mutex> lock(gDebugMutex);
            AddCircle(base, u, v, radius, color, duration, 32);
            AddLine(a, base + u * radius, color, duration);
            AddLine(a, base - u * radius, color, duration);
            AddLine(a, base + v * radius, color, duration);
            AddLine(a, base - v * radius, color, duration);
            AddLine(a, base, color, duration);
        }

        void SHARD_CALL DbgTriangles(const float* vertices, uint32_t count, uint32_t color, float duration, uint32_t)
        {
            if (!vertices) return;
            std::lock_guard<std::mutex> lock(gDebugMutex);
            for (uint32_t i = 0; i < count && gTriangles.size() < kMaxDebugLines; ++i) {
                const float* t = vertices + i * 9;
                if (!Finite3(t) || !Finite3(t + 3) || !Finite3(t + 6)) continue;
                gTriangles.push_back({ V3(t), V3(t + 3), V3(t + 6), color, duration });
            }
        }

        void SHARD_CALL DbgText(const float position[3], ShardStr text, uint32_t color, float duration, uint32_t)
        {
            if (!position || !Finite3(position) || !text.data || text.size == 0) return;
            std::lock_guard<std::mutex> lock(gDebugMutex);
            if (gTexts.size() < 4096) gTexts.push_back({ V3(position), false, std::string(text.data, size_t(std::min<uint64_t>(text.size, 256))), color, duration });
        }

        void SHARD_CALL DbgTextScreen(float x, float y, ShardStr text, uint32_t color, float duration)
        {
            if (!std::isfinite(x) || !std::isfinite(y) || !text.data || text.size == 0) return;
            std::lock_guard<std::mutex> lock(gDebugMutex);
            if (gTexts.size() < 4096) gTexts.push_back({ glm::vec3(x, y, 0), true, std::string(text.data, size_t(std::min<uint64_t>(text.size, 256))), color, duration });
        }

        void SHARD_CALL DbgClear()
        {
            std::lock_guard<std::mutex> lock(gDebugMutex);
            gLines.clear();
            gTriangles.clear();
            gTexts.clear();
        }

        // ---- Drawing the shapes ----

        struct Glyph { char c; const char* rows; };

        // 3x5 pixel glyphs of what a debug label needs ('#' is a pixel) ; lower case reads as upper case
        const Glyph kGlyphs[] = {
            {'0', "###" "#.#" "#.#" "#.#" "###"}, {'1', ".#." "##." ".#." ".#." "###"}, {'2', "###" "..#" "###" "#.." "###"},
            {'3', "###" "..#" "###" "..#" "###"}, {'4', "#.#" "#.#" "###" "..#" "..#"}, {'5', "###" "#.." "###" "..#" "###"},
            {'6', "###" "#.." "###" "#.#" "###"}, {'7', "###" "..#" ".#." ".#." ".#."}, {'8', "###" "#.#" "###" "#.#" "###"},
            {'9', "###" "#.#" "###" "..#" "###"},
            {'A', ".#." "#.#" "###" "#.#" "#.#"}, {'B', "##." "#.#" "##." "#.#" "##."}, {'C', "###" "#.." "#.." "#.." "###"},
            {'D', "##." "#.#" "#.#" "#.#" "##."}, {'E', "###" "#.." "##." "#.." "###"}, {'F', "###" "#.." "##." "#.." "#.."},
            {'G', "###" "#.." "#.#" "#.#" "###"}, {'H', "#.#" "#.#" "###" "#.#" "#.#"}, {'I', "###" ".#." ".#." ".#." "###"},
            {'J', "..#" "..#" "..#" "#.#" "###"}, {'K', "#.#" "#.#" "##." "#.#" "#.#"}, {'L', "#.." "#.." "#.." "#.." "###"},
            {'M', "#.#" "###" "###" "#.#" "#.#"}, {'N', "##." "#.#" "#.#" "#.#" "#.#"}, {'O', "###" "#.#" "#.#" "#.#" "###"},
            {'P', "###" "#.#" "###" "#.." "#.."}, {'Q', "###" "#.#" "#.#" "###" "..#"}, {'R', "###" "#.#" "##." "#.#" "#.#"},
            {'S', "###" "#.." "###" "..#" "###"}, {'T', "###" ".#." ".#." ".#." ".#."}, {'U', "#.#" "#.#" "#.#" "#.#" "###"},
            {'V', "#.#" "#.#" "#.#" "#.#" ".#."}, {'W', "#.#" "#.#" "###" "###" "#.#"}, {'X', "#.#" "#.#" ".#." "#.#" "#.#"},
            {'Y', "#.#" "#.#" ".#." ".#." ".#."}, {'Z', "###" "..#" ".#." "#.." "###"},
            {'.', "..." "..." "..." "..." ".#."}, {',', "..." "..." "..." ".#." "#.."}, {':', "..." ".#." "..." ".#." "..."},
            {'-', "..." "..." "###" "..." "..."}, {'+', "..." ".#." "###" ".#." "..."}, {'/', "..#" "..#" ".#." "#.." "#.."},
            {'(', ".#." "#.." "#.." "#.." ".#."}, {')', ".#." "..#" "..#" "..#" ".#."}, {'%', "#.#" "..#" ".#." "#.." "#.#"},
            {'_', "..." "..." "..." "..." "###"}, {'=', "..." "###" "..." "###" "..."}, {'!', ".#." ".#." ".#." "..." ".#."},
            {'?', "###" "..#" ".#." "..." ".#."},
        };

        const char* GlyphOf(char c)
        {
            const char up = char(std::toupper(static_cast<unsigned char>(c)));
            for (const Glyph& g : kGlyphs) if (g.c == up) return g.rows;
            return c == ' ' ? nullptr : kGlyphs[sizeof(kGlyphs) / sizeof(kGlyphs[0]) - 1].rows;      // an unknown character is a question mark
        }

        // A world position seen from the snapshot's camera
        struct Projector {
            glm::mat4 viewProjection{ 1.0f };
            Core::WorldPosition origin;
            float width = 1, height = 1;
            bool ok = false;

            glm::vec4 Clip(const glm::vec3& world) const
            {
                const glm::vec3 relative = Core::WorldPosition::FromDVec3(glm::dvec3(world)).RelativeTo(origin);
                return viewProjection * glm::vec4(relative, 1.0f);
            }
            glm::vec2 Screen(const glm::vec4& clip) const
            {
                const glm::vec2 ndc = glm::vec2(clip) / clip.w;
                return glm::vec2((ndc.x * 0.5f + 0.5f) * width, (1.0f - (ndc.y * 0.5f + 0.5f)) * height);
            }
        };

        uint32_t Premultiply(uint32_t color)
        {
            const uint32_t a = color >> 24;
            const uint32_t r = ((color & 0xFF) * a + 127) / 255, g = (((color >> 8) & 0xFF) * a + 127) / 255, b = (((color >> 16) & 0xFF) * a + 127) / 255;
            return r | (g << 8) | (b << 16) | (a << 24);
        }

        void AddQuad(OverlayProxy& proxy, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, const glm::vec2& d, uint32_t color)
        {
            const uint32_t base = uint32_t(proxy.inlineVertices.size());
            for (const glm::vec2& p : { a, b, c, d }) {
                OverlayVertex v;
                v.x = p.x; v.y = p.y; v.color = color;
                proxy.inlineVertices.push_back(v);
            }
            for (uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u }) proxy.inlineIndices.push_back(base + i);
        }

        void DrawLineSegment(OverlayProxy& proxy, const Projector& pr, glm::vec3 a, glm::vec3 b, uint32_t color)
        {
            glm::vec4 ca = pr.Clip(a), cb = pr.Clip(b);
            constexpr float kNear = 0.01f;
            if (ca.w < kNear && cb.w < kNear) return;
            if (ca.w < kNear) ca = ca + (cb - ca) * ((kNear - ca.w) / (cb.w - ca.w));
            else if (cb.w < kNear) cb = cb + (ca - cb) * ((kNear - cb.w) / (ca.w - cb.w));
            const glm::vec2 sa = pr.Screen(ca), sb = pr.Screen(cb);
            glm::vec2 d = sb - sa;
            const float length = glm::length(d);
            if (!(length > 1e-3f) || length > 1e5f) return;
            d /= length;
            const glm::vec2 n(-d.y * 0.75f, d.x * 0.75f);       // a line 1.5 pixels thick
            AddQuad(proxy, sa + n, sb + n, sb - n, sa - n, color);
        }

        void DrawText(OverlayProxy& proxy, glm::vec2 at, const std::string& text, uint32_t color)
        {
            constexpr float kScale = 2.0f;
            const float startX = at.x;
            for (char c : text) {
                if (c == '\n') { at.x = startX; at.y += 6.0f * kScale + 2.0f; continue; }
                if (const char* rows = GlyphOf(c)) {
                    for (int y = 0; y < 5; ++y) {
                        for (int x = 0; x < 3; ++x) {
                            if (rows[y * 3 + x] != '#') continue;
                            const glm::vec2 p = at + glm::vec2(float(x), float(y)) * kScale;
                            AddQuad(proxy, p, p + glm::vec2(kScale, 0), p + glm::vec2(kScale, kScale), p + glm::vec2(0, kScale), color);
                        }
                    }
                }
                at.x += 4.0f * kScale;
            }
        }

        // What shard.debug drew, as one overlay over the viewport (nothing if nothing is shown)
        void ExtractDebug(RenderSnapshot& next)
        {
            std::lock_guard<std::mutex> lock(gDebugMutex);
            if (gLines.empty() && gTriangles.empty() && gTexts.empty()) return;

            Projector pr;
            pr.width = float(std::max(next.viewportWidth, 1));
            pr.height = float(std::max(next.viewportHeight, 1));
            pr.origin = next.renderOrigin;
            pr.ok = next.camera.valid;
            pr.viewProjection = next.camera.viewProjection;

            OverlayProxy proxy;
            proxy.order = std::numeric_limits<int32_t>::max();
            proxy.width = uint32_t(pr.width);
            proxy.height = uint32_t(pr.height);

            if (pr.ok) {
                for (const DebugTriangle& t : gTriangles) {
                    const glm::vec4 a = pr.Clip(t.a), b = pr.Clip(t.b), c = pr.Clip(t.c);
                    if (a.w < 0.01f || b.w < 0.01f || c.w < 0.01f) continue;
                    const uint32_t color = Premultiply(t.color);
                    const uint32_t base = uint32_t(proxy.inlineVertices.size());
                    for (const glm::vec4& p : { a, b, c }) {
                        const glm::vec2 s = pr.Screen(p);
                        OverlayVertex v;
                        v.x = s.x; v.y = s.y; v.color = color;
                        proxy.inlineVertices.push_back(v);
                    }
                    for (uint32_t i = 0; i < 3; ++i) proxy.inlineIndices.push_back(base + i);
                }
                for (const DebugLine& l : gLines)
                    DrawLineSegment(proxy, pr, l.a, l.b, Premultiply(l.color));
            }
            for (const DebugText& t : gTexts) {
                glm::vec2 at;
                if (t.screen) at = glm::vec2(t.position.x, t.position.y);
                else {
                    if (!pr.ok) continue;
                    const glm::vec4 clip = pr.Clip(t.position);
                    if (clip.w < 0.01f) continue;
                    at = pr.Screen(clip);
                }
                DrawText(proxy, at, t.text, Premultiply(t.color));
            }
            if (proxy.inlineIndices.empty()) return;
            OverlayCommand c;
            c.kind = OverlayCommand::Kind::Triangles;
            c.first = 0;
            c.count = uint32_t(proxy.inlineIndices.size());
            proxy.commands.push_back(std::move(c));
            next.overlays.push_back(std::move(proxy));
        }
    }

    // ---- The overlay API ----

    ShardResult SHARD_CALL OverlayAdd(ShardPluginCtx handle, const ShardOverlayDesc* desc, ShardOverlay* out)
    {
        try {
            if (!out || !desc || desc->structSize < offsetof(ShardOverlayDesc, user) || !desc->draw) return SHARD_E_INVALID_ARG;
            *out = 0;
            PluginContext* ctx = FindContext(handle);
            if (!ctx) return SHARD_E_STALE_HANDLE;
            if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
            HookRenderRelease(*ctx, PluginOwner(handle));
            auto rec = std::make_shared<OverlayRec>();
            rec->owner = PluginOwner(handle);
            rec->name = ToString(desc->name);
            rec->order = desc->order;
            rec->flags = desc->flags;
            rec->target = desc->target;
            rec->draw = desc->draw;
            rec->user = desc->user;
            *out = gOverlays.Add(PluginOwner(handle), rec);
            return SHARD_OK;
        } catch (...) { return SHARD_E_INTERNAL; }
    }

    ShardResult SHARD_CALL OverlayRemove(ShardPluginCtx handle, ShardOverlay overlay)
    {
        return gOverlays.Remove(PluginOwner(handle), overlay) ? SHARD_OK : SHARD_E_STALE_HANDLE;
    }

    ShardResult SHARD_CALL GeometryCreate(ShardPluginCtx handle, const ShardVertex2D* vertices, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount, ShardGeometry* out)
    {
        try {
            if (!out) return SHARD_E_INVALID_ARG;
            *out = 0;
            PluginContext* ctx = FindContext(handle);
            if (!ctx) return SHARD_E_STALE_HANDLE;
            if (!vertices || !indices || vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0 || vertexCount > (1u << 22) || indexCount > (1u << 24)) return SHARD_E_INVALID_ARG;
            HookRenderRelease(*ctx, PluginOwner(handle));
            auto g = std::make_shared<OverlayGeometry>();
            g->vertices.resize(vertexCount);
            for (uint32_t i = 0; i < vertexCount; ++i) {
                g->vertices[i].x = vertices[i].position[0];
                g->vertices[i].y = vertices[i].position[1];
                g->vertices[i].color = vertices[i].color;
                g->vertices[i].u = vertices[i].uv[0];
                g->vertices[i].v = vertices[i].uv[1];
            }
            g->indices.assign(indices, indices + indexCount);
            for (uint32_t& index : g->indices) index = std::min(index, vertexCount - 1);
            *out = gGeometries.Add(PluginOwner(handle), g);
            return SHARD_OK;
        } catch (...) { return SHARD_E_INTERNAL; }
    }

    ShardResult SHARD_CALL GeometryDestroy(ShardPluginCtx handle, ShardGeometry geometry)
    {
        return gGeometries.Remove(PluginOwner(handle), geometry) ? SHARD_OK : SHARD_E_STALE_HANDLE;
    }

    OwnedTable<OverlayGeometry>& Geometries() { return gGeometries; }

    void ReleaseOverlays(PluginOwner owner)
    {
        gOverlays.RemoveOwner(owner);
        gGeometries.RemoveOwner(owner);
    }

    void ExtractOverlays(const RenderSnapshot&, RenderSnapshot& next)
    {
        Core::IEngineContext* engine = Core::gSharedEnginePtr;
        if (!engine) return;
        const bool playing = engine->IsInPlayMode();

        // The overlays are copied out first : a callback may add or remove overlays
        struct Entry { uint64_t handle; OverlayRec rec; };
        std::vector<Entry> entries;
        gOverlays.ForEachAll([&](uint64_t handle, OverlayRec& rec, PluginOwner) { entries.push_back({ handle, rec }); });
        std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.rec.order < b.rec.order; });

        float dpi = 1.0f;
        if (Core::Platform::IWindow* window = engine->GetWindow()) dpi = window->GetContentScale();
        const float delta = engine->GetTimeManager() ? engine->GetTimeManager()->GetDeltaTime() : 0.0f;

        for (const Entry& entry : entries) {
            const OverlayRec& rec = entry.rec;
            if (!(rec.flags & SHARD_OVERLAY_IN_EDITOR) && !playing) continue;
            PluginContext* ctx = FindContext(ShardPluginCtx(rec.owner));
            if (!ctx || !gOverlays.Get(rec.owner, entry.handle)) continue;

            OverlayProxy proxy;
            proxy.order = rec.order;
            if (rec.target) {
                proxy.target = ResolveRenderTarget(rec.owner, rec.target);
                if (!proxy.target) continue;
                proxy.width = proxy.target->GetWidth();
                proxy.height = proxy.target->GetHeight();
            } else {
                proxy.width = uint32_t(std::max(next.viewportWidth, 1));
                proxy.height = uint32_t(std::max(next.viewportHeight, 1));
            }

            ShardOverlayFrame frame{};
            frame.structSize = sizeof(ShardOverlayFrame);
            frame.width = proxy.width;
            frame.height = proxy.height;
            frame.dpiScale = dpi;
            frame.deltaTime = delta;
            frame.frame = next.frame;
            frame.playing = playing ? SHARD_TRUE : SHARD_FALSE;
            frame.originX = rec.target ? 0.0f : gViewportOrigin[0];
            frame.originY = rec.target ? 0.0f : gViewportOrigin[1];

            Recording recording{ &proxy, rec.owner };
            Recording* previous = tRecording;
            tRecording = &recording;
            {
                ActiveScope scope(ActiveCounter(*ctx), rec.owner);
                ScriptScope scriptScope;
                try { rec.draw(rec.user, ShardPluginCtx(rec.owner), &frame, &kDraw); }
                catch (const std::exception& e) { if (ctx->Bindings().reportError) ctx->Bindings().reportError(rec.owner, "overlay '" + rec.name + "' threw : " + e.what()); }
                catch (...) { if (ctx->Bindings().reportError) ctx->Bindings().reportError(rec.owner, "overlay '" + rec.name + "' threw"); }
            }
            tRecording = previous;
            if (!proxy.commands.empty()) next.overlays.push_back(std::move(proxy));
        }

        ExtractDebug(next);
    }

    void DebugClearAll() { DbgClear(); }

    void DebugEndFrame()
    {
        std::lock_guard<std::mutex> lock(gDebugMutex);
        if (gLines.empty() && gTriangles.empty() && gTexts.empty()) return;
        Core::IEngineContext* engine = Core::gSharedEnginePtr;
        const float deltaSeconds = engine && engine->GetTimeManager() ? engine->GetTimeManager()->GetDeltaTime() : 0.0f;
        auto expired = [&](auto& list) {
            list.erase(std::remove_if(list.begin(), list.end(), [&](auto& s) { s.remaining -= deltaSeconds; return s.remaining <= 0.0f; }), list.end());
        };
        expired(gLines);
        expired(gTriangles);
        expired(gTexts);
    }

    void SetPluginOverlayOrigin(float x, float y)
    {
        gViewportOrigin[0] = x;
        gViewportOrigin[1] = y;
    }

    const void* DebugApi()
    {
        static const ShardDebugApi api = {
            sizeof(ShardDebugApi), &DbgEnabled, &DbgLine, &DbgLines, &DbgPoint, &DbgArrow, &DbgCircle, &DbgSphere, &DbgBox, &DbgCapsule, &DbgCone,
            &DbgTriangles, &DbgText, &DbgTextScreen, &DbgClear };
        return &api;
    }
}

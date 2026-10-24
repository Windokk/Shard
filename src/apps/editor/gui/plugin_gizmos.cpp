#include <cmath>
#include <cstring>

#include "imgui/imgui.h"

#include <shard/shard_editor.h>

#include "apps/editor/gui/plugin_ui.hpp"
#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/world/actor.hpp"

// shard.editor, ShardGizmoApi : what a plugin's gizmo draws over the viewport. The shapes are lines in the world, projected with the camera of the
// viewport and drawn with ImGui's draw list of the viewport window, clipped to the image.

namespace Shard::Editor::GUI {

    namespace {
        struct Painter {
            glm::mat4 viewProjection{ 1.0f };
            ImVec2 min, max;
            ImDrawList* list = nullptr;
        } gPainter;

        ImU32 Color(const float c[4])
        {
            auto channel = [](float v) { return ImU32(std::fmax(0.0f, std::fmin(1.0f, v)) * 255.0f + 0.5f); };
            return IM_COL32(channel(c[0]), channel(c[1]), channel(c[2]), channel(c[3]));
        }

        // The pixel a point of the world is at ; false if it is behind the camera
        bool Project(const glm::vec3& p, ImVec2& out)
        {
            const glm::vec4 clip = gPainter.viewProjection * glm::vec4(p, 1.0f);
            if (clip.w <= 1.0e-5f) return false;
            const float w = gPainter.max.x - gPainter.min.x, h = gPainter.max.y - gPainter.min.y;
            out = ImVec2(gPainter.min.x + (clip.x / clip.w * 0.5f + 0.5f) * w, gPainter.min.y + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * h);
            return true;
        }

        // A segment, cut where it crosses the plane of the camera so that a line that goes behind it still shows its visible part
        void Segment(glm::vec3 a, glm::vec3 b, ImU32 color, float thickness = 1.5f)
        {
            glm::vec4 ca = gPainter.viewProjection * glm::vec4(a, 1.0f), cb = gPainter.viewProjection * glm::vec4(b, 1.0f);
            constexpr float kNear = 1.0e-3f;
            if (ca.w < kNear && cb.w < kNear) return;
            if (ca.w < kNear) { const float t = (kNear - ca.w) / (cb.w - ca.w); a = a + (b - a) * t; }
            else if (cb.w < kNear) { const float t = (kNear - cb.w) / (ca.w - cb.w); b = b + (a - b) * t; }
            ImVec2 pa, pb;
            if (Project(a, pa) && Project(b, pb)) gPainter.list->AddLine(pa, pb, color, thickness);
        }

        glm::vec3 V(const float v[3]) { return glm::vec3(v[0], v[1], v[2]); }

        void Circle(const glm::vec3& center, glm::vec3 normal, float radius, ImU32 color)
        {
            if (glm::dot(normal, normal) < 1.0e-12f) return;
            normal = glm::normalize(normal);
            const glm::vec3 helper = std::fabs(normal.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            const glm::vec3 u = glm::normalize(glm::cross(normal, helper)), v = glm::cross(normal, u);
            constexpr int kSegments = 40;
            glm::vec3 previous = center + u * radius;
            for (int i = 1; i <= kSegments; ++i) {
                const float angle = 6.2831853f * float(i) / float(kSegments);
                const glm::vec3 next = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
                Segment(previous, next, color);
                previous = next;
            }
        }

        void SHARD_CALL Line(const float from[3], const float to[3], const float rgba[4]) { Segment(V(from), V(to), Color(rgba)); }
        void SHARD_CALL Point(const float position[3], float size, const float rgba[4])
        {
            ImVec2 p;
            if (Project(V(position), p)) gPainter.list->AddCircleFilled(p, std::fmax(size, 1.0f), Color(rgba));
        }
        void SHARD_CALL CircleFn(const float center[3], const float normal[3], float radius, const float rgba[4]) { Circle(V(center), V(normal), radius, Color(rgba)); }
        void SHARD_CALL Sphere(const float center[3], float radius, const float rgba[4])
        {
            const ImU32 c = Color(rgba);
            Circle(V(center), glm::vec3(1, 0, 0), radius, c);
            Circle(V(center), glm::vec3(0, 1, 0), radius, c);
            Circle(V(center), glm::vec3(0, 0, 1), radius, c);
        }
        void SHARD_CALL Box(const float center[3], const float half[3], const float rgba[4])
        {
            const ImU32 c = Color(rgba);
            const glm::vec3 m = V(center), h = V(half);
            auto corner = [&](int i) { return m + glm::vec3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z); };
            for (int i = 0; i < 8; ++i)
                for (int bit = 1; bit < 8; bit <<= 1)
                    if (!(i & bit)) Segment(corner(i), corner(i | bit), c);
        }
        void SHARD_CALL Arrow(const float from[3], const float to[3], const float rgba[4])
        {
            const ImU32 c = Color(rgba);
            Segment(V(from), V(to), c, 2.0f);
            ImVec2 a, b;
            if (!Project(V(from), a) || !Project(V(to), b)) return;
            const float dx = a.x - b.x, dy = a.y - b.y, length = std::sqrt(dx * dx + dy * dy);
            if (length < 1.0f) return;
            const float ux = dx / length, uy = dy / length, head = std::fmin(12.0f, length * 0.5f);
            gPainter.list->AddLine(b, ImVec2(b.x + (ux * 0.8f - uy * 0.5f) * head, b.y + (uy * 0.8f + ux * 0.5f) * head), c, 2.0f);
            gPainter.list->AddLine(b, ImVec2(b.x + (ux * 0.8f + uy * 0.5f) * head, b.y + (uy * 0.8f - ux * 0.5f) * head), c, 2.0f);
        }
        void SHARD_CALL Text(const float position[3], ShardStr text, const float rgba[4])
        {
            ImVec2 p;
            if (Project(V(position), p) && text.data) gPainter.list->AddText(p, Color(rgba), text.data, text.data + text.size);
        }

        ShardGizmoApi MakeGizmos()
        {
            ShardGizmoApi g;
            std::memset(&g, 0, sizeof(g));
            g.structSize = sizeof(ShardGizmoApi);
            g.line = &Line; g.point = &Point; g.circle = &CircleFn; g.sphere = &Sphere; g.box = &Box; g.arrow = &Arrow; g.text = &Text;
            return g;
        }
        const ShardGizmoApi kGizmos = MakeGizmos();
    }

    void DrawPluginGizmos(const glm::mat4& viewProjection, const float min[2], const float max[2],
                          const std::vector<std::shared_ptr<Engine::Objects::Actor>>& selected)
    {
        using namespace Engine::Plugins;
        const auto gizmos = PluginManager::Get().Extensions().Get<EditorGizmoItem>("editor.gizmo");
        if (gizmos.empty() || selected.empty()) return;
        gPainter.viewProjection = viewProjection;
        gPainter.min = ImVec2(min[0], min[1]);
        gPainter.max = ImVec2(max[0], max[1]);
        gPainter.list = ImGui::GetWindowDrawList();
        gPainter.list->PushClipRect(gPainter.min, gPainter.max, true);
        for (const auto& actor : selected) {
            const uint64_t handle = EditorActorHandle(actor);
            for (const auto& gizmo : gizmos)
                if (gizmo->draw) gizmo->draw(&kGizmos, handle);
        }
        gPainter.list->PopClipRect();
        gPainter.list = nullptr;
    }
}

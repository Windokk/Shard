// Shard C++ SDK : drawing for the developer, in the world, from anywhere (shard_debug.h).
//
//   Shard::Debug::Line(agent.position, agent.destination, Shard::Rgba(0, 255, 0));                 // for this frame
//   Shard::Debug::Sphere(sensor.center, sensor.radius, Shard::Rgba(255, 255, 0, 128), 2.f);        // for two seconds
//   if (Shard::Debug::Enabled()) for (const auto& edge : navmesh.Edges()) Shard::Debug::Line(edge.a, edge.b, kEdge);
//
// A shape lives `duration` seconds (0 : one frame), so a system that draws what it computes calls the functions again every frame. thread : any.
#pragma once

#include <string_view>
#include <vector>

#include <shard/shard_debug.h>
#include <shard/cpp/detail.hpp>
#include <shard/cpp/math.hpp>

namespace Shard {

    /// 8 bit r, g, b, a packed as the ABI wants
    constexpr uint32_t Rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) { return (r & 255u) | ((g & 255u) << 8) | ((b & 255u) << 16) | ((a & 255u) << 24); }

    class Debug {
    public:
        /// False when this host draws nothing (a server) : skip the work of computing shapes
        static bool Enabled() { const auto* api = Try(); return api && api->enabled() == SHARD_TRUE; }

        static void Line(Vec3 from, Vec3 to, uint32_t color = Rgba(255, 255, 255), float duration = 0.f, bool depthTest = false)
        {
            const float a[3] = { from.x, from.y, from.z }, b[3] = { to.x, to.y, to.z };
            if (const auto* api = Try()) api->line(a, b, color, duration, Flags(depthTest));
        }
        /// Segments : the points taken two by two (0-1, 2-3...)
        static void Lines(const std::vector<Vec3>& points, uint32_t color = Rgba(255, 255, 255), float duration = 0.f, bool depthTest = false)
        {
            if (const auto* api = Try()) api->lines(reinterpret_cast<const float*>(points.data()), uint32_t(points.size()), color, duration, Flags(depthTest));
        }
        static void Point(Vec3 position, float size = 0.1f, uint32_t color = Rgba(255, 255, 255), float duration = 0.f)
        {
            const float p[3] = { position.x, position.y, position.z };
            if (const auto* api = Try()) api->point(p, size, color, duration, 0);
        }
        static void Arrow(Vec3 from, Vec3 to, uint32_t color = Rgba(255, 255, 255), float duration = 0.f)
        {
            const float a[3] = { from.x, from.y, from.z }, b[3] = { to.x, to.y, to.z };
            if (const auto* api = Try()) api->arrow(a, b, color, duration, 0);
        }
        static void Circle(Vec3 center, Vec3 normal, float radius, uint32_t color = Rgba(255, 255, 255), float duration = 0.f)
        {
            const float c[3] = { center.x, center.y, center.z }, n[3] = { normal.x, normal.y, normal.z };
            if (const auto* api = Try()) api->circle(c, n, radius, color, duration, 0);
        }
        static void Sphere(Vec3 center, float radius, uint32_t color = Rgba(255, 255, 255), float duration = 0.f)
        {
            const float c[3] = { center.x, center.y, center.z };
            if (const auto* api = Try()) api->sphere(c, radius, color, duration, 0);
        }
        static void Box(Vec3 center, Vec3 halfExtent, uint32_t color = Rgba(255, 255, 255), float duration = 0.f, const Quat* rotation = nullptr)
        {
            const float c[3] = { center.x, center.y, center.z }, h[3] = { halfExtent.x, halfExtent.y, halfExtent.z };
            const float q[4] = { rotation ? rotation->x : 0.f, rotation ? rotation->y : 0.f, rotation ? rotation->z : 0.f, rotation ? rotation->w : 1.f };
            if (const auto* api = Try()) api->box(c, h, rotation ? q : nullptr, color, duration, 0);
        }
        static void Capsule(Vec3 from, Vec3 to, float radius, uint32_t color = Rgba(255, 255, 255), float duration = 0.f)
        {
            const float a[3] = { from.x, from.y, from.z }, b[3] = { to.x, to.y, to.z };
            if (const auto* api = Try()) api->capsule(a, b, radius, color, duration, 0);
        }
        /// A cone from its apex along `axis` (its length is the cone's) with a half opening of `halfAngleDegrees` : a field of view
        static void Cone(Vec3 apex, Vec3 axis, float halfAngleDegrees, uint32_t color = Rgba(255, 255, 255), float duration = 0.f)
        {
            const float a[3] = { apex.x, apex.y, apex.z }, x[3] = { axis.x, axis.y, axis.z };
            if (const auto* api = Try()) api->cone(a, x, halfAngleDegrees, color, duration, 0);
        }
        /// Filled triangles (3 points each), the colour's alpha honoured : a navigation mesh's polygons
        static void Triangles(const std::vector<Vec3>& vertices, uint32_t color, float duration = 0.f)
        {
            if (const auto* api = Try()) api->triangles(reinterpret_cast<const float*>(vertices.data()), uint32_t(vertices.size() / 3), color, duration, 0);
        }
        static void Text(Vec3 position, std::string_view text, uint32_t color = Rgba(255, 255, 255), float duration = 0.f)
        {
            const float p[3] = { position.x, position.y, position.z };
            if (const auto* api = Try()) api->text(p, Str(text), color, duration, 0);
        }
        /// Text at a pixel of the viewport (from the top left)
        static void TextScreen(float x, float y, std::string_view text, uint32_t color = Rgba(255, 255, 255), float duration = 0.f)
        {
            if (const auto* api = Try()) api->text_screen(x, y, Str(text), color, duration);
        }
        /// Removes the shapes still shown for a duration
        static void Clear() { if (const auto* api = Try()) api->clear(); }

    private:
        static uint32_t Flags(bool depthTest) { return depthTest ? SHARD_DEBUG_DEPTH_TEST : 0u; }
        // A host without the interface (an older engine) draws nothing, instead of failing the plugin
        static const ShardDebugApi* Try()
        {
            static const ShardDebugApi* api = nullptr;
            if (!api) api = detail::Interface<ShardDebugApi>(SHARD_IFACE_DEBUG);
            return api;
        }
    };
}

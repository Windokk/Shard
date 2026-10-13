#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering {

    /// One material slot's geometry, as UnwrapForLightmap() reads it. Plain arrays, so it has no idea what the mesh's vertex
    /// format is.
    struct LightmapUnwrapGroup
    {
        const glm::vec3* positions = nullptr;   // per vertex
        const glm::vec3* normals = nullptr;     // per vertex, may be null
        size_t vertexCount = 0;
        const uint32_t* indices = nullptr;      // a triangle list over the vertices
        size_t indexCount = 0;
    };

    struct LightmapUnwrapSettings
    {
        /// A chart is a patch of connected triangles that can be flattened onto one plane : a triangle joins it while its
        /// normal stays within this angle of the chart's average normal (lower : more, flatter charts, less distortion but
        /// more seams)...
        float maxChartAngleDegrees = 45.0f;
        /// ...and within this angle of the chart's first triangle, so a chart cannot keep drifting around a curve
        float maxSeedAngleDegrees = 70.0f;
        /// Gap kept around every chart, in units of the unit square the layout lives in (so its texels, once the lightmap has
        /// a resolution : padding * resolution). The empty border is what keeps bilinear sampling, and the filling of the
        /// texels next to a chart, from reaching into another chart.
        float padding = 2.0f / 128.0f;
        /// A chart smaller than this (in square mesh units, i.e. m2) is merged into the neighbouring chart it shares most edges
        /// with, unless that one faces more than maxMergeAngleDegrees away from it. Charts a few texels wide are all seam :
        /// they show as patches in the lightmap (0 : never merge)
        float minChartArea = 0.03f;
        float maxMergeAngleDegrees = 75.0f;
    };

    /// One group's geometry once cut into charts : the vertices that sit on a chart seam are split, one copy per chart.
    struct LightmapUnwrapGroupResult
    {
        std::vector<uint32_t> sourceVertex;     // per output vertex : the input vertex it is a copy of
        std::vector<glm::vec2> uv;              // per output vertex : its lightmap coordinates, in [0, 1]
        std::vector<uint32_t> indices;          // a triangle list over the output vertices, in the order of the input's
    };

    struct LightmapUnwrapResult
    {
        bool success = false;                   // false : nothing to unwrap (no triangle with an area)
        std::vector<LightmapUnwrapGroupResult> groups;      // one per input group, same order
        uint32_t chartCount = 0;
        float surfaceArea = 0.0f;               // the triangles' total area in the mesh's own units
        float uvArea = 0.0f;                    // their total area in the unit square, the share of it the layout covers
        bool fromCache = false;                 // the layout was read from the cache instead of being worked out (see SetLightmapUnwrapCacheDirectory)
    };

    /// @brief Gives a mesh a second set of texture coordinates for baked lighting (lightmaps) : every surface of the mesh gets
    /// its own place in the unit square, no two triangles overlap, and one world unit of surface takes the same number of
    /// texels everywhere (a lightmap is then as sharp on a small chart as on a large one).
    ///
    /// The surface is cut into charts (patches whose triangles face about the same way, grown from triangle to triangle across
    /// shared edges), each chart is flattened by projecting it along its average normal, turned to take the least room, and
    /// the charts are packed into the unit square, all at the same scale. Deterministic : the same mesh always gives the same
    /// layout, which a baked lightmap relies on. The groups are laid out together in one square, but a chart never spans two
    /// groups (a material slot is its own draw).
    LightmapUnwrapResult UnwrapForLightmap(const std::vector<LightmapUnwrapGroup>& groups, const LightmapUnwrapSettings& settings = {});

    /// @brief Where UnwrapForLightmap() keeps the layouts it worked out. Cutting a mesh of millions of triangles takes seconds
    /// (a minute in a debug build) and the very same mesh is unwrapped every time it loads : the layout, a pure function of
    /// the geometry and the settings, is stored in a file named after a hash of them (derived data, safe to delete) and read
    /// back the next time. An empty directory (the default) turns the cache off. The files are written atomically, so
    /// several threads unwrapping at once are safe. Set it once, at startup.
    void SetLightmapUnwrapCacheDirectory(const std::string& directory);
}

#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "engine/renderer/features/lighting/lightmap_data.hpp"

namespace Shard::Engine::Rendering {

    /// One model instance to give a lightmap to
    struct LightmapInstanceInput
    {
        uint64_t key = 0;               // what the caller knows the instance by (the render proxy's key)
        float surfaceArea = 0.0f;       // its surface in the world, square meters
        float uvArea = 0.0f;            // the share of its mesh's lightmap square its triangles cover (see UnwrapForLightmap)
        uint32_t resolutionOverride = 0;    // a resolution asked for, in texels per side (0 : worked out from the density)
    };

    /// Where one instance's lightmap sits in the atlas : a square of `resolution` texels at (x, y) of page `page`
    struct LightmapPlacement
    {
        uint64_t key = 0;
        uint32_t page = 0;
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t resolution = 0;

        /// What turns a coordinate of the mesh's lightmap square into one of the page : uv * xy + zw
        glm::vec4 ScaleOffset(uint32_t pageSize) const
        {
            const float size = float(pageSize);
            return glm::vec4(float(resolution) / size, float(resolution) / size, float(x) / size, float(y) / size);
        }
    };

    struct LightmapLayout
    {
        uint32_t pageSize = 0;                  // every page is pageSize x pageSize texels
        uint32_t pageCount = 0;
        std::vector<LightmapPlacement> placements;     // in the order of the inputs
    };

    /// The lightmap resolution of an instance : the number of texels per side at which its surface gets
    /// settings.texelsPerMeter, given that its mesh's layout only covers `uvArea` of the lightmap square. Clamped to the
    /// settings' range (an override is clamped to it as well).
    uint32_t LightmapResolutionFor(const LightmapInstanceInput& instance, const LightmapSettings& settings);

    /// @brief Gives every instance a place in the atlas : each gets a square lightmap (see LightmapResolutionFor), the squares
    /// are packed into pages of the smallest power of two that holds them, or into several pages of settings.maxPageSize when
    /// one is not enough. A gap of two texels is left around each so that the filtering at the edge of a lightmap never reads
    /// the one next to it. Deterministic.
    LightmapLayout PlanLightmapLayout(const std::vector<LightmapInstanceInput>& instances, const LightmapSettings& settings);
}

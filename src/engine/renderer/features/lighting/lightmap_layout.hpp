#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "engine/renderer/features/lighting/lightmap_data.hpp"

namespace Shard::Engine::Rendering {

    struct LightmapInstanceInput
    {
        uint64_t key = 0;
        float surfaceArea = 0.0f;
        float uvArea = 0.0f;
        uint32_t resolutionOverride = 0;
    };

    struct LightmapPlacement
    {
        uint64_t key = 0;
        uint32_t page = 0;
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t resolution = 0;

        glm::vec4 ScaleOffset(uint32_t pageSize) const
        {
            const float size = float(pageSize);
            return glm::vec4(float(resolution) / size, float(resolution) / size, float(x) / size, float(y) / size);
        }
    };

    struct LightmapLayout
    {
        uint32_t pageSize = 0;
        uint32_t pageCount = 0;
        std::vector<LightmapPlacement> placements;
    };

    uint32_t LightmapResolutionFor(const LightmapInstanceInput& instance, const LightmapSettings& settings);

    LightmapLayout PlanLightmapLayout(const std::vector<LightmapInstanceInput>& instances, const LightmapSettings& settings);
}

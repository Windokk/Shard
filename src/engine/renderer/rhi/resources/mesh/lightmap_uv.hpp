#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering {

    struct LightmapUnwrapGroup
    {
        const glm::vec3* positions = nullptr;
        const glm::vec3* normals = nullptr;
        size_t vertexCount = 0;
        const uint32_t* indices = nullptr;
        size_t indexCount = 0;
    };

    struct LightmapUnwrapSettings
    {
        float maxChartAngleDegrees = 45.0f;
        float maxSeedAngleDegrees = 70.0f;
        float padding = 2.0f / 128.0f;
        float minChartArea = 0.03f;
        float maxMergeAngleDegrees = 75.0f;
    };

    struct LightmapUnwrapGroupResult
    {
        std::vector<uint32_t> sourceVertex;
        std::vector<glm::vec2> uv;
        std::vector<uint32_t> indices;
    };

    struct LightmapUnwrapResult
    {
        bool success = false;
        std::vector<LightmapUnwrapGroupResult> groups;
        uint32_t chartCount = 0;
        float surfaceArea = 0.0f;
        float uvArea = 0.0f;
        bool fromCache = false;
    };

    // Gives each surface its own place in the unit square at a uniform texel density. Deterministic, baked lightmaps rely on it.
    // Charts never span two groups (a material slot is its own draw).
    LightmapUnwrapResult UnwrapForLightmap(const std::vector<LightmapUnwrapGroup>& groups, const LightmapUnwrapSettings& settings = {});

    void SetLightmapUnwrapCacheDirectory(const std::string& directory);
}

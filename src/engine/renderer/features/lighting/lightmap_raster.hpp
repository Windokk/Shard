#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering {

    struct LightmapRasterInstance
    {
        const uint8_t* vertices = nullptr;
        size_t vertexStride = 0;
        uint32_t positionOffset = 0;
        int normalOffset = -1;
        uint32_t lightmapUVOffset = 0;
        const uint32_t* indices = nullptr;
        size_t indexCount = 0;
        glm::mat4 worldMatrix = glm::mat4(1.0f);

        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t resolution = 0;
    };

    struct LightmapPageRaster
    {
        uint32_t size = 0;
        std::vector<float> position;
        std::vector<float> normal;
        std::vector<float> geometric;
        std::vector<uint32_t> texelList;

        bool IsCovered(uint32_t x, uint32_t y) const { return position[(size_t(y) * size + x) * 4 + 3] > 0.0f; }
    };

    std::shared_ptr<LightmapPageRaster> RasterizeLightmapPage(uint32_t pageSize, const std::vector<LightmapRasterInstance>& instances);
}

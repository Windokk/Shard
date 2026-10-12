#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering {

    /// One mesh instance of a lightmap page, as RasterizeLightmapPage() reads it : the mesh's vertices and triangles, where it
    /// stands in the world, and which square of the page its lightmap coordinates are laid out on.
    struct LightmapRasterInstance
    {
        const uint8_t* vertices = nullptr;      // the mesh's vertex data, `vertexStride` bytes a vertex
        size_t vertexStride = 0;
        uint32_t positionOffset = 0;            // byte offsets in a vertex : a vec3...
        int normalOffset = -1;                  // ... a vec3, or -1 when the mesh has no normals...
        uint32_t lightmapUVOffset = 0;          // ... and a vec2, the lightmap coordinates in [0, 1]
        const uint32_t* indices = nullptr;      // a triangle list
        size_t indexCount = 0;
        glm::mat4 worldMatrix = glm::mat4(1.0f);

        uint32_t x = 0;                         // the lightmap's place in the page : `resolution` texels from (x, y)
        uint32_t y = 0;
        uint32_t resolution = 0;
    };

    /// What the baker needs to know of every texel of a page : which surface point it stands for and which way it faces.
    /// All the arrays are size * size texels, the bottom row first, four floats each.
    struct LightmapPageRaster
    {
        uint32_t size = 0;
        std::vector<float> position;            // xyz : world position ; w : the world size of one texel there (0 : no surface)
        std::vector<float> normal;              // xyz : the smooth normal (interpolated from the vertices)
        std::vector<float> geometric;           // xyz : the normal of the triangle, on the side the smooth normal faces
        std::vector<uint32_t> texelList;        // (y << 16) | x of every covered texel, row after row

        bool IsCovered(uint32_t x, uint32_t y) const { return position[(size_t(y) * size + x) * 4 + 3] > 0.0f; }
    };

    /// @brief Draws the lightmap layout of every instance into a page of `pageSize` x `pageSize` texels : for each triangle,
    /// the texels whose centre falls inside it get the point of the surface they stand for (position, normal) by barycentric
    /// interpolation, in world space. A triangle too thin to cover a texel centre still gets the texel it passes through, so no
    /// part of a surface is left without light. Instances whose lightmaps overlap would overwrite each other : the layout
    /// (PlanLightmapLayout) never does that.
    std::shared_ptr<LightmapPageRaster> RasterizeLightmapPage(uint32_t pageSize, const std::vector<LightmapRasterInstance>& instances);
}

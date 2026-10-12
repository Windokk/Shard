#include "lightmap_raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Shard::Engine::Rendering {

    namespace {

        glm::vec3 ReadVec3(const uint8_t* vertex, uint32_t offset)
        {
            glm::vec3 v;
            std::memcpy(&v, vertex + offset, sizeof(v));
            return v;
        }

        glm::vec2 ReadVec2(const uint8_t* vertex, uint32_t offset)
        {
            glm::vec2 v;
            std::memcpy(&v, vertex + offset, sizeof(v));
            return v;
        }

        // The point of a triangle a texel stands for
        struct TexelSample
        {
            glm::vec3 position;
            glm::vec3 normal;
            glm::vec3 geometric;
            float texelWorldSize;
        };

        void WriteTexel(LightmapPageRaster& raster, uint32_t x, uint32_t y, const TexelSample& sample)
        {
            const size_t i = (size_t(y) * raster.size + x) * 4;
            raster.position[i + 0] = sample.position.x;
            raster.position[i + 1] = sample.position.y;
            raster.position[i + 2] = sample.position.z;
            raster.position[i + 3] = sample.texelWorldSize;
            raster.normal[i + 0] = sample.normal.x;
            raster.normal[i + 1] = sample.normal.y;
            raster.normal[i + 2] = sample.normal.z;
            raster.normal[i + 3] = 1.0f;
            raster.geometric[i + 0] = sample.geometric.x;
            raster.geometric[i + 1] = sample.geometric.y;
            raster.geometric[i + 2] = sample.geometric.z;
            raster.geometric[i + 3] = 1.0f;
        }
    }

    std::shared_ptr<LightmapPageRaster> RasterizeLightmapPage(uint32_t pageSize, const std::vector<LightmapRasterInstance>& instances)
    {
        auto raster = std::make_shared<LightmapPageRaster>();
        raster->size = pageSize;
        const size_t count = size_t(pageSize) * pageSize * 4;
        raster->position.assign(count, 0.0f);
        raster->normal.assign(count, 0.0f);
        raster->geometric.assign(count, 0.0f);

        const int size = int(pageSize);

        for (const LightmapRasterInstance& instance : instances)
        {
            if (!instance.vertices || !instance.indices || instance.resolution == 0)
                continue;

            const glm::mat3 linear(instance.worldMatrix);
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));
            const float res = float(instance.resolution);

            for (size_t t = 0; t + 2 < instance.indexCount; t += 3)
            {
                const uint8_t* vertex[3] = {
                    instance.vertices + size_t(instance.indices[t]) * instance.vertexStride,
                    instance.vertices + size_t(instance.indices[t + 1]) * instance.vertexStride,
                    instance.vertices + size_t(instance.indices[t + 2]) * instance.vertexStride,
                };

                glm::vec3 world[3];
                glm::vec3 smooth[3];
                glm::vec2 texel[3];     // the lightmap coordinates, in texels of the page
                for (int k = 0; k < 3; k++)
                {
                    world[k] = glm::vec3(instance.worldMatrix * glm::vec4(ReadVec3(vertex[k], instance.positionOffset), 1.0f));
                    smooth[k] = instance.normalOffset >= 0 ? normalMatrix * ReadVec3(vertex[k], uint32_t(instance.normalOffset)) : glm::vec3(0.0f);
                    const glm::vec2 uv = ReadVec2(vertex[k], instance.lightmapUVOffset);
                    texel[k] = glm::vec2(float(instance.x) + uv.x * res, float(instance.y) + uv.y * res);
                }

                const glm::vec3 faceVector = glm::cross(world[1] - world[0], world[2] - world[0]);
                const float faceLength = glm::length(faceVector);
                const float worldArea = 0.5f * faceLength;
                if (faceLength < 1e-20f)
                    continue;
                const glm::vec3 faceNormal = faceVector / faceLength;

                const float texelArea2 = (texel[1].x - texel[0].x) * (texel[2].y - texel[0].y) - (texel[1].y - texel[0].y) * (texel[2].x - texel[0].x);
                if (std::abs(texelArea2) < 1e-9f)
                    continue;

                // The size of a texel on this triangle : the square root of the world area one texel of the layout covers
                const float texelWorldSize = std::sqrt(worldArea / (0.5f * std::abs(texelArea2)));

                auto sampleAt = [&](float w0, float w1, float w2) {
                    TexelSample sample;
                    sample.position = world[0] * w0 + world[1] * w1 + world[2] * w2;
                    glm::vec3 n = smooth[0] * w0 + smooth[1] * w1 + smooth[2] * w2;
                    const float nl = glm::length(n);
                    n = nl > 1e-12f ? n / nl : faceNormal;
                    sample.normal = n;
                    sample.geometric = glm::dot(faceNormal, n) < 0.0f ? -faceNormal : faceNormal;
                    sample.texelWorldSize = texelWorldSize;
                    return sample;
                };

                const int x0 = std::max(0, int(std::floor(std::min({ texel[0].x, texel[1].x, texel[2].x }))));
                const int x1 = std::min(size - 1, int(std::ceil(std::max({ texel[0].x, texel[1].x, texel[2].x }))));
                const int y0 = std::max(0, int(std::floor(std::min({ texel[0].y, texel[1].y, texel[2].y }))));
                const int y1 = std::min(size - 1, int(std::ceil(std::max({ texel[0].y, texel[1].y, texel[2].y }))));

                bool coveredAny = false;
                for (int y = y0; y <= y1; y++)
                {
                    for (int x = x0; x <= x1; x++)
                    {
                        const glm::vec2 p(float(x) + 0.5f, float(y) + 0.5f);
                        const float w0 = ((texel[1].x - p.x) * (texel[2].y - p.y) - (texel[1].y - p.y) * (texel[2].x - p.x)) / texelArea2;
                        const float w1 = ((texel[2].x - p.x) * (texel[0].y - p.y) - (texel[2].y - p.y) * (texel[0].x - p.x)) / texelArea2;
                        const float w2 = 1.0f - w0 - w1;
                        constexpr float kInside = -1e-5f;
                        if (w0 < kInside || w1 < kInside || w2 < kInside)
                            continue;

                        coveredAny = true;
                        if (!raster->IsCovered(uint32_t(x), uint32_t(y)))
                            WriteTexel(*raster, uint32_t(x), uint32_t(y), sampleAt(w0, w1, w2));
                    }
                }

                // Too thin to contain a texel centre : give the triangle the texel its centre falls in, so it is not left dark
                if (!coveredAny)
                {
                    const glm::vec2 centre = (texel[0] + texel[1] + texel[2]) / 3.0f;
                    const int cx = int(std::floor(centre.x)), cy = int(std::floor(centre.y));
                    if (cx >= 0 && cy >= 0 && cx < size && cy < size && !raster->IsCovered(uint32_t(cx), uint32_t(cy)))
                        WriteTexel(*raster, uint32_t(cx), uint32_t(cy), sampleAt(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f));
                }
            }
        }

        for (uint32_t y = 0; y < pageSize; y++)
            for (uint32_t x = 0; x < pageSize; x++)
                if (raster->IsCovered(x, y))
                    raster->texelList.push_back((y << 16) | x);

        return raster;
    }
}

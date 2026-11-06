#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <memory>

#include <glm/glm.hpp>

#include "engine/renderer/rhi/render_snapshot.hpp"

namespace Shard::Engine::Rendering {
    class Mesh;
    struct MeshCPUCopy;
    class Material;
}

namespace Shard::Engine::Rendering::Raytracing {

    struct BVHNode;

    // Hot buffer is read every BVH step, cold only for the final hit. Mirrors std430, keep in sync with path_trace.comp.
    struct GPUTrianglePos
    {
        glm::vec4 v0, v1, v2;
    };

    struct GPUTriangleAttrib
    {
        glm::vec4 n0, n1, n2;
        glm::vec4 uvMatID;
        glm::vec4 t0, t1, t2;
    };

    // Bindless handles as two uint32 halves so GLSL needs no int64. Valid only when the textureFlags bit is set.
    struct GPUMaterial
    {
        glm::vec4 albedo   = glm::vec4(1.0f);
        glm::vec4 emissive = glm::vec4(0.0f);
        float roughness = 0.9f;
        float metallic  = 0.0f;
        float ior       = 1.5f;
        uint32_t textureFlags = 0;
        glm::uvec2 albedoTex    = glm::uvec2(0);
        glm::uvec2 metallicTex  = glm::uvec2(0);
        glm::uvec2 roughnessTex = glm::uvec2(0);
        glm::uvec2 normalTex    = glm::uvec2(0);
        glm::uvec2 emissiveTex  = glm::uvec2(0);
        uint32_t flags          = 0;
        uint32_t padding        = 0;
    };

    // Materials SSBO is a raw memcpy of this struct. A size not a multiple of 16 skews every later material.
    static_assert(sizeof(GPUMaterial) % 16 == 0, "GPUMaterial must match the std430 array stride of its GLSL twin");

    enum GPUMaterialTextureBit : uint32_t
    {
        GPUMaterialTexAlbedo    = 1u << 0,
        GPUMaterialTexMetallic  = 1u << 1,
        GPUMaterialTexRoughness = 1u << 2,
        GPUMaterialTexNormal    = 1u << 3,
        GPUMaterialTexEmissive  = 1u << 4,
    };

    enum GPUMaterialFlagBit : uint32_t
    {
        // Camera and bounce rays pass through back faces, like the raster cull. Shadow rays hit both faces, as shadow maps draw both.
        GPUMaterialDoubleSided = 1u << 0,
    };

    // Emitter list is the CDF of power, so a binary search on a random number picks one proportionally (NEE).
    struct GPUEmissiveTriangle
    {
        uint32_t triangle = 0;
        float cdf = 0.0f;
        float padding0 = 0.0f;
        float padding1 = 0.0f;
    };
    static_assert(sizeof(GPUEmissiveTriangle) == 16, "GPUEmissiveTriangle must match the std430 stride of its GLSL twin");

    struct RaytraceScene
    {
        std::vector<BVHNode> bvhNodes;
        std::vector<GPUTrianglePos> trianglePositions;
        std::vector<GPUTriangleAttrib> triangleAttribs;
        std::vector<GPUMaterial> materials;

        std::vector<GPUEmissiveTriangle> emissiveTriangles;
        float emissivePower = 0.0f;
    };

    // Captured on the main thread so flatten + BVH build can run on a worker without touching live state.
    struct ModelSnapshot
    {
        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<const MeshCPUCopy> cpu;
        std::vector<uint32_t> materialIndicesPerSubmesh;
        glm::mat4 worldMatrix = glm::mat4(1.0f);

        uint64_t key = 0;
        int lightmapResolution = 0;
    };

    struct RaytraceSceneSnapshot
    {
        std::vector<ModelSnapshot> models;
        std::vector<GPUMaterial> materials;
    };

    constexpr uint32_t kSkippedSubmesh = 0xFFFFFFFFu;

    GPUMaterial ExtractMaterial(const std::shared_ptr<Material>& mat);

    class SceneBuilder
    {
        public:
            // May be called from the build worker thread, so it must be cheap and thread-safe.
            using ProgressCallback = std::function<void(float, const char*)>;

            static RaytraceScene Build(const RenderSnapshot& scene);

            // Main thread only (resolves bindless handles). excludeMasked drops alpha-cutout submeshes: probe rays treat foliage cards as solid and flicker.
            static RaytraceSceneSnapshot CaptureSnapshot(const RenderSnapshot& scene, bool excludeMasked = false);

            // Pure CPU, safe on any thread.
            static RaytraceScene BuildFromSnapshot(const RaytraceSceneSnapshot& snapshot,
                const ProgressCallback& onProgress = {});
    };

}

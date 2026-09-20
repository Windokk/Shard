#pragma once

#include <memory>
#include <cstdint>

namespace Shard::Engine::Rendering {

    class Renderer;
    class StorageBuffer;
    class ComputeShader;
    class ComputePipeline;

    // Forward+ (clustered) light culling for point/spot lights - see common/cluster.glsl for why
    // directional lights are excluded. Mirrors ProbeManager/SSAOManager's shape: owns its GPU
    // resources, Init() once from Renderer::Init(), Update() once per frame from Renderer::BeginFrame().
    // Update() dispatches two compute passes (cluster_build.comp, then cluster_light_cull.comp) that
    // fill the LightGrid/LightIndexList SSBOs mesh/lit.frag reads directly (bindings 2/3), replacing its
    // former full-array light scan for point/spot lights.
    //
    // The cluster grid is rebuilt from the active camera's current projection every frame rather than
    // dirty-tracked (mirrors how SSAOManager recomputes noiseScale every Update() instead of caching it) -
    // cluster_build.comp only depends on view/projection/near/far/resolution, all cheap to redo (a few
    // thousand threads of simple unprojection math), so there's no dirty flag to get wrong when fov/
    // near/far/viewport size change at runtime.
    class LightCullingManager
    {
        public:
            void Init(Renderer* renderer);

            // Rebuilds the cluster AABB grid and re-culls lights into it. Called once per frame from
            // Renderer::BeginFrame(), after LightManager/ShadowManager have applied this frame's light
            // mutations (so the cull pass reads up-to-date light data) and before ForwardPass executes.
            void Update();

            /// @brief Re-binds the light grid / index list to the slots lit.frag reads them from (2/3).
            /// For callers that temporarily bound something else there (see ImmediateRenderer).
            void BindShadingBuffers();

            uint32_t GetGridSizeX() const { return m_GridSizeX; }
            uint32_t GetGridSizeY() const { return m_GridSizeY; }
            float GetClusterScaleZ() const { return m_ClusterScaleZ; }
            float GetClusterBiasZ() const { return m_ClusterBiasZ; }

        private:
            // Mirrors common/cluster.glsl - keep in sync, see that file's header comment.
            static constexpr int kTilePx = 64;
            static constexpr int kZSlices = 24;
            static constexpr int kMaxLightsPerCluster = 128;

            std::shared_ptr<StorageBuffer> m_ClusterAABBBuffer;
            std::shared_ptr<StorageBuffer> m_LightGridBuffer;
            std::shared_ptr<StorageBuffer> m_LightIndexListBuffer;
            std::shared_ptr<StorageBuffer> m_GlobalIndexCounterBuffer;

            std::shared_ptr<ComputeShader> m_BuildShader;
            std::shared_ptr<ComputePipeline> m_BuildPipeline;

            std::shared_ptr<ComputeShader> m_CullShader;
            std::shared_ptr<ComputePipeline> m_CullPipeline;

            uint32_t m_GridSizeX = 0;
            uint32_t m_GridSizeY = 0;
            uint32_t m_TotalClusters = 0;

            float m_ClusterScaleZ = 0.0f;
            float m_ClusterBiasZ = 0.0f;
    };
}

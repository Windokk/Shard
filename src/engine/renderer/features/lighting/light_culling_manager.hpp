#pragma once

#include <memory>
#include <cstdint>

namespace Shard::Engine::Rendering {

    class IRenderContext;
    class StorageBuffer;
    class ComputeShader;
    class ComputePipeline;

    class LightCullingManager
    {
        public:
            void Init(IRenderContext* renderer);

            void Update();

            void BindShadingBuffers();

            uint32_t GetGridSizeX() const { return m_GridSizeX; }
            uint32_t GetGridSizeY() const { return m_GridSizeY; }
            float GetClusterScaleZ() const { return m_ClusterScaleZ; }
            float GetClusterBiasZ() const { return m_ClusterBiasZ; }

        private:
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

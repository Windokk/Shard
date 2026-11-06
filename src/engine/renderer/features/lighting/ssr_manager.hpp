#pragma once

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering {

    class IRenderContext;
    class Material;
    class Texture2D;
    class ComputeShader;
    class ComputePipeline;

    // Screen-space reflections: Hi-Z, colour pyramid of last frame, hierarchical march, edge-aware resolve. lit.frag picks the pyramid level by roughness (the prepass has no materials).
    class SSRManager
    {
        public:
            static constexpr const char* kPassName = "SSRPass";

            void Init(IRenderContext* renderer);

            bool IsReady() const { return m_Ready; }

            void Update();

            void Execute(uint32_t sceneColor);

            void BindSSR(const std::shared_ptr<Material>& material, bool allowed);

        private:
            bool EnsureResources(uint32_t width, uint32_t height, int divisor);

            void Dispatch(const std::shared_ptr<ComputePipeline>& pipeline, uint32_t width, uint32_t height);

            IRenderContext* m_Renderer = nullptr;
            bool m_Ready = false;

            std::shared_ptr<ComputeShader> m_HiZInitShader, m_HiZReduceShader, m_ColorInitShader, m_ColorReduceShader, m_TraceShader, m_ResolveShader;
            std::shared_ptr<ComputePipeline> m_HiZInitPipeline, m_HiZReducePipeline, m_ColorInitPipeline, m_ColorReducePipeline, m_TracePipeline, m_ResolvePipeline;

            std::shared_ptr<Texture2D> m_HiZ;
            std::shared_ptr<Texture2D> m_Pyramid;
            std::shared_ptr<Texture2D> m_Result;
            std::shared_ptr<Texture2D> m_History[2];
            uint32_t m_Width = 0, m_Height = 0;
            uint32_t m_TraceWidth = 0, m_TraceHeight = 0;
            int m_Divisor = 0;
            uint32_t m_HiZLevels = 0;
            uint32_t m_PyramidLevels = 0;
            uint64_t m_ResultHandle = 0;
            uint64_t m_PyramidHandle = 0;

            int m_HistoryIndex = 0;
            bool m_HistoryValid = false;

            glm::mat4 m_ViewProj = glm::mat4(1.0f);
            glm::mat4 m_PrevViewProj = glm::mat4(1.0f);
            glm::vec3 m_PrevPosition = glm::vec3(0.0f);
            glm::vec3 m_PrevForward = glm::vec3(0.0f, 0.0f, -1.0f);
            bool m_HaveCamera = false;
            float m_CameraStill = 1.0f;

            bool m_Produced = false;
    };
}

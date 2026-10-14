#pragma once

#include <unordered_map>

#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"

#include "vk_api.hpp"
#include "vk_shader.hpp"

namespace Shard::Engine::Rendering {

    /// What a draw asks of a pipeline besides its own specifications : the way the API is told to draw it right now
    struct PipelineVariant
    {
        bool depthOnly = false;         ///< the depth sweep : nothing is written to the colour attachment
        bool wireframe = false;         ///< every triangle is drawn as lines (the editor's debug views)
    };

    /// @brief A pipeline of the rhi : the specifications the renderer gives it (a shader, what the blending does...), made into the Vulkan pipelines
    /// that draw them for each kind of render target and of vertices they meet. The state that can change per draw (culling, the depth test) is
    /// dynamic, the rest is part of the pipeline, which is created the first time it is drawn with and kept.
    class VKPipeline final : public Pipeline
    {
        public:

            explicit VKPipeline(const PipelineSpecifications& specs) { m_Specifications = specs; }
            ~VKPipeline() override;

            void Invalidate() override;
            void Bind() override {}

            /// The pipeline to draw with. Null if the shader is not usable.
            VkPipeline Get(const VK::RenderTarget& target, const VertexLayout& vertices, const PipelineVariant& variant);

            VKShader* GetShader() const;

        private:

            VkPipeline Create(const VK::RenderTarget& target, const VertexLayout& vertices, const PipelineVariant& variant, VKShader& shader);

            std::unordered_map<uint64_t, VkPipeline> m_Variants;
    };

    class VKComputePipeline final : public ComputePipeline
    {
        public:

            explicit VKComputePipeline(const ComputePipelineSpecifications& specs) { m_Specifications = specs; }
            ~VKComputePipeline() override;

            void Bind() override {}

            VkPipeline Get();
            VKComputeShader* GetShader() const;

        private:

            VkPipeline m_Pipeline = VK_NULL_HANDLE;
            uint32_t m_BuiltVersion = 0;
    };

}

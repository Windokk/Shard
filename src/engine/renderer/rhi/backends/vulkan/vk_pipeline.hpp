#pragma once

#include <unordered_map>

#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"

#include "vk_api.hpp"
#include "vk_shader.hpp"

namespace Shard::Engine::Rendering {

    struct PipelineVariant
    {
        bool depthOnly = false;
        bool wireframe = false;
    };

    class VKPipeline final : public Pipeline
    {
        public:

            explicit VKPipeline(const PipelineSpecifications& specs) { m_Specifications = specs; }
            ~VKPipeline() override;

            void Invalidate() override;
            void Bind() override {}

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

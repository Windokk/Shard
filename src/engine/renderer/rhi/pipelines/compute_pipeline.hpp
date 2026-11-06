#pragma once

#include "engine/renderer/rhi/shader/compute_shader.hpp"

namespace Shard::Engine::Rendering{

    struct ComputePipelineSpecifications
    {
        std::shared_ptr<ComputeShader> shader;

        std::string debugName;

        bool operator==(const ComputePipelineSpecifications& other) const;
    };

    struct ComputePipelineSpecsHash
    {
        std::size_t operator()(const ComputePipelineSpecifications& s) const;
    };

    class ComputePipeline
    {
        public:

            virtual ~ComputePipeline() = default;

            ComputePipelineSpecifications& GetSpecifications()
            {
                return m_Specifications;
            };

            virtual void Bind() = 0;


        protected:
            ComputePipelineSpecifications m_Specifications;

        private:
            static std::shared_ptr<ComputePipeline> Create(const ComputePipelineSpecifications& specs);

        friend class Renderer;
    };
}
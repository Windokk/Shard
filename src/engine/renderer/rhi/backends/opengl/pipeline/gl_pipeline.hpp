#pragma once

#include "engine/renderer/rhi/pipelines/pipeline.hpp"

namespace Shard::Engine::Rendering{
    class GLPipeline : public Pipeline
    {
    public:

        GLPipeline(const PipelineSpecifications& specs);

        void Invalidate() override;

        void Bind() override;
        
    private:

    };
}
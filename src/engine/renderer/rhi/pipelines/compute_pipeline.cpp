#include "compute_pipeline.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/backends/opengl/pipeline/gl_compute_pipeline.hpp"

#include "engine/renderer/frontend/renderer.hpp"

#include "engine/renderer/material/compute_shader.hpp"

namespace Shard::Engine::Rendering
{
    std::shared_ptr<ComputePipeline> ComputePipeline::Create(const ComputePipelineSpecifications &specs)
    {
        switch(Core::GetEngine().GetRenderer()->GetRendererAPI()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return std::make_shared<GLComputePipeline>(specs);

            /*case RendererAPI::API::Vulkan:
                return std::make_shared<VKFramebuffer>(spec);

            case RendererAPI::API::DX11:
                return std::make_shared<DX11Framebuffer>(spec);

            case RendererAPI::API::DX12:
                return std::make_shared<DX12Framebuffer>(spec);*/

            default:
                return nullptr;
        }
    }

    std::size_t ComputePipelineSpecsHash::operator()(const ComputePipelineSpecifications &s) const
    {
        std::size_t hash = 0;

        uint64_t shaderID = s.shader ? s.shader->GetAssetID().GetAsInt() : 0;
        HashCombine(hash, std::hash<uint64_t>()(shaderID));

        return hash;
    }
    
    bool ComputePipelineSpecifications::operator==(const ComputePipelineSpecifications &other) const
    {
        uint64_t idA = shader ? shader->GetAssetID().GetAsInt() : 0;
        uint64_t idB = other.shader ? other.shader->GetAssetID().GetAsInt() : 0;

        return idA == idB && idA != 0 && idB != 0;
    }
}
#include "pipeline.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/shader/shader.hpp"

#include "engine/renderer/rhi/backends/opengl/pipeline/gl_pipeline.hpp"

#include "engine/renderer/rhi/renderer_api.hpp"

namespace Shard::Engine::Rendering
{
    std::shared_ptr<Pipeline> Pipeline::Create(const PipelineSpecifications &specs)
    {
        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return std::make_shared<GLPipeline>(specs);

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
    
    uint32_t VertexElement::Size() const
    {
        return ShaderDataTypeSize(type);
    }

    std::size_t PipelineSpecsHash::operator()(const PipelineSpecifications &s) const
    {
        std::size_t hash = 0;

        uint64_t shaderID = s.shader ? s.shader->GetAssetID().GetAsInt() : 0;
        HashCombine(hash, std::hash<uint64_t>()(shaderID));

        HashCombine(hash, VertexLayoutHash{}(s.vertexLayout));

        HashCombine(hash, (int)s.topology);
        HashCombine(hash, (int)s.cullMode);
        HashCombine(hash, (int)s.polygonMode);

        HashCombine(hash, s.depthTest);
        HashCombine(hash, s.depthWrite);
        HashCombine(hash, (int)s.depthCompare);

        HashCombine(hash, s.blending);

        if (s.blending)
        {
            HashCombine(hash, (int)s.srcBlend);
            HashCombine(hash, (int)s.dstBlend);
            HashCombine(hash, (int)s.blendOp);
        }

        return hash;
    }
    
    bool PipelineSpecifications::operator==(const PipelineSpecifications &other) const
    {
        uint64_t idA = shader ? shader->GetAssetID().GetAsInt() : 0;
        uint64_t idB = other.shader ? other.shader->GetAssetID().GetAsInt() : 0;

        return idA == idB &&
            vertexLayout == other.vertexLayout &&
            topology == other.topology &&
            cullMode == other.cullMode &&
            polygonMode == other.polygonMode &&
            depthTest == other.depthTest &&
            depthWrite == other.depthWrite &&
            depthCompare == other.depthCompare &&
            blending == other.blending &&
            srcBlend == other.srcBlend &&
            dstBlend == other.dstBlend &&
            blendOp == other.blendOp;
    }
}
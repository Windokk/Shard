#include "pipeline.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/shader/shader.hpp"

#include "engine/renderer/rhi/backends/opengl/pipeline/gl_pipeline.hpp"
#include "engine/renderer/rhi/backends/null/null_rhi.hpp"
#include "engine/renderer/rhi/backends/vulkan/vk_pipeline.hpp"

#include "engine/renderer/rhi/renderer_api.hpp"

namespace Shard::Engine::Rendering
{
    std::shared_ptr<Pipeline> Pipeline::Create(const PipelineSpecifications &specs)
    {
        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return std::make_shared<GLPipeline>(specs);

            case RendererAPI::API::Vulkan:
                return std::make_shared<VKPipeline>(specs);

            case RendererAPI::API::Null:
                return std::make_shared<Null::NullPipeline>(specs);

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
    
    std::shared_ptr<Pipeline> Pipeline::GetVariant(const std::string& define)
    {
        std::shared_ptr<Shader> shader = m_Specifications.shader ? m_Specifications.shader->GetVariant(define) : nullptr;
        if (!shader)
            return nullptr;

        // What a material did to this pipeline since it was made (its blending) applies to the variant as well
        PipelineSpecifications specs = m_Specifications;
        specs.shader = shader;

        std::shared_ptr<Pipeline>& variant = m_Variants[define];
        if (!variant)
            variant = Create(specs);
        else
            variant->GetSpecifications() = specs;

        return variant;
    }

    uint32_t VertexElement::Size() const
    {
        return ShaderDataTypeSize(type);
    }

    std::size_t PipelineSpecsHash::operator()(const PipelineSpecifications &s) const
    {
        std::size_t hash = 0;

        // The shader object, not its AssetID : the shaders made from files directly (not assets) all have the ID 0
        HashCombine(hash, std::hash<const Shader*>()(s.shader.get()));

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
        return shader == other.shader &&
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
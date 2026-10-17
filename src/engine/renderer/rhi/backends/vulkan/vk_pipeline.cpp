#include "vk_pipeline.hpp"

#include <vector>

namespace Shard::Engine::Rendering {

    using namespace VK;

    namespace {

        void DestroyLater(const std::vector<VkPipeline>& pipelines)
        {
            if (pipelines.empty() || !Context::Exists())
                return;

            VkDevice device = Context::Get().GetDevice();
            Context::Get().Defer([device, pipelines] {
                for (VkPipeline pipeline : pipelines)
                    vkDestroyPipeline(device, pipeline, nullptr);
            });
        }

        uint64_t LayoutKey(const VertexLayout& layout)
        {
            uint64_t key = layout.GetStride();
            for (const VertexElement& element : layout.GetElements())
            {
                key = HashCombine64(key, element.location);
                key = HashCombine64(key, static_cast<uint64_t>(element.type));
                key = HashCombine64(key, element.offset);
            }
            return key;
        }
    }

    // ---- VKPipeline ----------------------------------------------------------------------------------------------------------------

    VKPipeline::~VKPipeline()
    {
        std::vector<VkPipeline> pipelines;
        for (auto& [key, pipeline] : m_Variants)
        {
            if (pipeline)
                pipelines.push_back(pipeline);
        }
        DestroyLater(pipelines);
    }

    void VKPipeline::Invalidate()
    {
        std::vector<VkPipeline> pipelines;
        for (auto& [key, pipeline] : m_Variants)
        {
            if (pipeline)
                pipelines.push_back(pipeline);
        }
        m_Variants.clear();
        DestroyLater(pipelines);
    }

    VKShader* VKPipeline::GetShader() const
    {
        return static_cast<VKShader*>(m_Specifications.shader.get());
    }

    VkPipeline VKPipeline::Get(const RenderTarget& target, const VertexLayout& vertices, const PipelineVariant& variant)
    {
        VKShader* shader = GetShader();
        if (!shader || !shader->Program().IsValid())
            return VK_NULL_HANDLE;

        const PipelineSpecifications& s = m_Specifications;

        // Everything that is baked into the pipeline. The specifications are read as they are now : a material changes the blending of the
        // pipeline it was given when it is made.
        uint64_t key = target.Key();
        key = HashCombine64(key, LayoutKey(vertices));
        key = HashCombine64(key, shader->Program().GetVersion());
        key = HashCombine64(key, shader->Program().GetId());
        key = HashCombine64(key, static_cast<uint64_t>(s.topology));
        key = HashCombine64(key, static_cast<uint64_t>(variant.wireframe || s.polygonMode == PolygonMode::Line));
        key = HashCombine64(key, static_cast<uint64_t>(variant.depthOnly));
        key = HashCombine64(key, static_cast<uint64_t>(s.blending));
        if (s.blending)
        {
            key = HashCombine64(key, static_cast<uint64_t>(s.srcBlend));
            key = HashCombine64(key, static_cast<uint64_t>(s.dstBlend));
            key = HashCombine64(key, static_cast<uint64_t>(s.blendOp));
        }

        auto it = m_Variants.find(key);
        if (it != m_Variants.end())
            return it->second;

        VkPipeline pipeline = Create(target, vertices, variant, *shader);
        m_Variants[key] = pipeline;
        return pipeline;
    }

    VkPipeline VKPipeline::Create(const RenderTarget& target, const VertexLayout& vertices, const PipelineVariant& variant, VKShader& shader)
    {
        VKProgram& program = shader.Program();
        const PipelineSpecifications& s = m_Specifications;

        std::vector<VkPipelineShaderStageCreateInfo> stages;
        for (ShaderStageKind kind : {ShaderStageKind::Vertex, ShaderStageKind::Geometry, ShaderStageKind::Fragment})
        {
            VkShaderModule module = program.GetModule(kind);
            if (!module)
                continue;

            VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            stage.stage = kind == ShaderStageKind::Vertex ? VK_SHADER_STAGE_VERTEX_BIT : (kind == ShaderStageKind::Geometry ? VK_SHADER_STAGE_GEOMETRY_BIT : VK_SHADER_STAGE_FRAGMENT_BIT);
            stage.module = module;
            stage.pName = "main";
            stages.push_back(stage);
        }

        // The vertices : the layout of the mesh, the attributes the shader reads
        VkVertexInputBindingDescription binding{0, vertices.GetStride(), VK_VERTEX_INPUT_RATE_VERTEX};
        std::vector<VkVertexInputAttributeDescription> attributes;
        for (const VertexElement& element : vertices.GetElements())
        {
            bool read = false;
            for (const ReflectedVertexInput& input : program.GetBinary().reflection.vertexInputs)
            {
                if (input.location == element.location)
                {
                    read = true;
                    break;
                }
            }
            if (!read)
                continue;

            VkVertexInputAttributeDescription attribute{};
            attribute.location = element.location;
            attribute.binding = 0;
            attribute.format = ToVkVertexFormat(element.type);
            attribute.offset = element.offset;
            attributes.push_back(attribute);
        }

        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = attributes.empty() ? 0u : 1u;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = ToVkTopology(s.topology);

        // OpenGL's clip space is kept (z from -1 to 1) : the depth the shaders see and write is the one they were written for
        VkPipelineViewportDepthClipControlCreateInfoEXT clipControl{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT};
        clipControl.negativeOneToOne = VK_TRUE;

        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.pNext = &clipControl;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        const bool lines = variant.wireframe || s.polygonMode == PolygonMode::Line;

        VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterization.polygonMode = lines ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;                 // dynamic
        // The viewport is not flipped (the images are stored the way OpenGL stores them), so a front face that is counter clockwise there is clockwise here
        rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterization.lineWidth = 1.0f;
        if (variant.wireframe)
        {
            // Lines win the depth test against a filled sweep of the same triangles
            rasterization.depthBiasEnable = VK_TRUE;
            rasterization.depthBiasConstantFactor = -1.0f;
            rasterization.depthBiasSlopeFactor = -1.0f;
        }

        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = target.samples;

        VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthStencil.minDepthBounds = 0.0f;
        depthStencil.maxDepthBounds = 1.0f;

        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = variant.depthOnly ? 0 : (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT);
        if (s.blending)
        {
            attachment.blendEnable = VK_TRUE;
            attachment.srcColorBlendFactor = attachment.srcAlphaBlendFactor = ToVkBlendFactor(s.srcBlend);
            attachment.dstColorBlendFactor = attachment.dstAlphaBlendFactor = ToVkBlendFactor(s.dstBlend);
            attachment.colorBlendOp = attachment.alphaBlendOp = ToVkBlendOp(s.blendOp);
        }

        VkPipelineColorBlendAttachmentState attachments[1 + RenderTarget::kMaxExtraColors];
        for (VkPipelineColorBlendAttachmentState& a : attachments)
            a = attachment;

        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = target.ColorCount();
        blend.pAttachments = attachments;

        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
            VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
        };
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = static_cast<uint32_t>(sizeof(dynamicStates) / sizeof(dynamicStates[0]));
        dynamic.pDynamicStates = dynamicStates;

        VkFormat colorFormats[1 + RenderTarget::kMaxExtraColors];
        for (uint32_t i = 0; i < target.ColorCount(); i++)
            colorFormats[i] = target.ColorAt(i)->format;

        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = target.ColorCount();
        rendering.pColorAttachmentFormats = colorFormats;
        rendering.depthAttachmentFormat = target.DepthFormat();
        rendering.stencilAttachmentFormat = target.depth && HasStencil(target.DepthFormat()) ? target.DepthFormat() : VK_FORMAT_UNDEFINED;

        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.stageCount = static_cast<uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &rasterization;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depthStencil;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = program.GetPipelineLayout();

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (VK_CHECK(vkCreateGraphicsPipelines(Context::Get().GetDevice(), Context::Get().GetPipelineCache(), 1, &info, nullptr, &pipeline)) != VK_SUCCESS)
        {
            DEBUG_ERROR("Could not create the pipeline '", s.debugName, "' for the shader ", program.GetName());
            return VK_NULL_HANDLE;
        }

        SetObjectName(VK_OBJECT_TYPE_PIPELINE, pipeline, s.debugName.empty() ? program.GetName() : s.debugName);
        return pipeline;
    }

    // ---- VKComputePipeline ---------------------------------------------------------------------------------------------------------

    VKComputePipeline::~VKComputePipeline()
    {
        if (m_Pipeline)
            DestroyLater({m_Pipeline});
    }

    VKComputeShader* VKComputePipeline::GetShader() const
    {
        return static_cast<VKComputeShader*>(m_Specifications.shader.get());
    }

    VkPipeline VKComputePipeline::Get()
    {
        VKComputeShader* shader = GetShader();
        if (!shader || !shader->Program().IsValid())
            return VK_NULL_HANDLE;

        VKProgram& program = shader->Program();

        if (m_Pipeline && m_BuiltVersion == program.GetVersion())
            return m_Pipeline;

        if (m_Pipeline)
        {
            DestroyLater({m_Pipeline});
            m_Pipeline = VK_NULL_HANDLE;
        }

        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = program.GetModule(ShaderStageKind::Compute);
        info.stage.pName = "main";
        info.layout = program.GetPipelineLayout();

        if (VK_CHECK(vkCreateComputePipelines(Context::Get().GetDevice(), Context::Get().GetPipelineCache(), 1, &info, nullptr, &m_Pipeline)) != VK_SUCCESS)
        {
            DEBUG_ERROR("Could not create the compute pipeline for the shader ", program.GetName());
            m_Pipeline = VK_NULL_HANDLE;
            return VK_NULL_HANDLE;
        }

        SetObjectName(VK_OBJECT_TYPE_PIPELINE, m_Pipeline, m_Specifications.debugName.empty() ? program.GetName() : m_Specifications.debugName);
        m_BuiltVersion = program.GetVersion();
        return m_Pipeline;
    }

}

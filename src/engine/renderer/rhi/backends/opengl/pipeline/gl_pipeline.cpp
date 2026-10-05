#include "gl_pipeline.hpp"

#include "engine/renderer/rhi/backends/opengl/gl_utils.hpp"

#include "engine/renderer/rhi/backends/opengl/shader/gl_shader.hpp"

namespace Shard::Engine::Rendering{
    
    GLPipeline::GLPipeline(const PipelineSpecifications& specs)
    {
        m_Specifications = specs;
    }

    void GLPipeline::Invalidate()
    {
        
    }

    void GLPipeline::Bind()
    {
        const auto& spec = m_Specifications;

        std::shared_ptr<GLShader> glShader = std::static_pointer_cast<GLShader>(spec.shader);

        GLStateCache::BindProgram(glShader->GetProgram());

        GLStateCache::SetDepthTest(spec.depthTest);
        GLStateCache::SetDepthWrite(spec.depthWrite);

        if(spec.depthTest)
        {
            GLenum depthFunc;

            switch(spec.depthCompare){
                case DepthCompareOp::Always:      depthFunc = GL_ALWAYS; break;
                case DepthCompareOp::Greater:     depthFunc = GL_GREATER; break;
                case DepthCompareOp::Less:        depthFunc = GL_LESS; break;
                case DepthCompareOp::LessOrEqual: depthFunc = GL_LEQUAL; break;
                case DepthCompareOp::Never:       depthFunc = GL_NEVER; break;
                default:                          depthFunc = GL_LESS; break;
            }

            GLStateCache::SetDepthFunc(depthFunc);
        }

        switch (spec.cullMode)
        {
            case CullMode::Back:
                GLStateCache::SetCullMode(true, GL_BACK);
                break;

            case CullMode::Front:
                GLStateCache::SetCullMode(true, GL_FRONT);
                break;

            case CullMode::None:
                GLStateCache::SetCullMode(false, GL_BACK);
                break;
        }

        GLStateCache::SetBlendEnabled(spec.blending);

        if (spec.blending)
        {
            GLenum srcBlend;
            GLenum dstBlend;

            switch(spec.srcBlend){
                case BlendFactor::One:              srcBlend = GL_ONE; break;
                case BlendFactor::OneMinusSrcAlpha: srcBlend = GL_ONE_MINUS_SRC_ALPHA; break;
                case BlendFactor::SrcAlpha:         srcBlend = GL_SRC_ALPHA; break;
                case BlendFactor::Zero:             srcBlend = GL_ZERO; break;
                default:                            srcBlend = GL_SRC_ALPHA; break;
            }

            switch(spec.dstBlend){
                case BlendFactor::One:              dstBlend = GL_ONE; break;
                case BlendFactor::OneMinusSrcAlpha: dstBlend = GL_ONE_MINUS_SRC_ALPHA; break;
                case BlendFactor::SrcAlpha:         dstBlend = GL_SRC_ALPHA; break;
                case BlendFactor::Zero:             dstBlend = GL_ZERO; break;
                default:                            dstBlend = GL_ONE_MINUS_SRC_ALPHA; break;
            }

            GLStateCache::SetBlendFunc(srcBlend, dstBlend);

            GLenum blendEquation;

            switch(spec.blendOp){
                case BlendOp::Add:      blendEquation = GL_FUNC_ADD; break;
                case BlendOp::Subtract: blendEquation = GL_FUNC_SUBTRACT; break;
                default:                blendEquation = GL_FUNC_ADD; break;
            }

            GLStateCache::SetBlendEquation(blendEquation);
        }

        GLenum polyMode;

        switch(spec.polygonMode){
            case PolygonMode::Fill:{
                polyMode = GL_FILL;
                break;
            }
            case PolygonMode::Line:{
                polyMode = GL_LINE;
                break;
            }
            default:{
                polyMode = GL_FILL;
                break;
            }
        }

        GLStateCache::SetPolygonMode(polyMode);
    }
}
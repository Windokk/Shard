#include "gl_material.hpp"

#include "engine/rendering/backends/opengl/gl_utils.hpp"
#include "engine/rendering/backends/opengl/pipeline/gl_pipeline.hpp"
#include "engine/rendering/shader/shader.hpp"
#include "engine/rendering/backends/opengl/shader/gl_shader.hpp"
#include "engine/rendering/backends/opengl/texture/gl_texture.hpp"

#include "engine/debugging/logger.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/resources/resources_manager.hpp"
#include "engine/filesystem/assetID.hpp"
#include "engine/rendering/renderer/renderer.hpp"

namespace Shard::Engine::Rendering{

    static GLenum ShaderSamplerTypeToOpenGL(ShaderSamplerType type)
    {
        switch(type)
        {
            case ShaderSamplerType::Texture1D:
            case ShaderSamplerType::Texture1DShadow:
                return GL_TEXTURE_1D;

            case ShaderSamplerType::Texture2D:
            case ShaderSamplerType::Texture2DShadow:
                return GL_TEXTURE_2D;

            case ShaderSamplerType::Texture3D:
                return GL_TEXTURE_3D;

            case ShaderSamplerType::TextureCube:
            case ShaderSamplerType::TextureCubeShadow:
                return GL_TEXTURE_CUBE_MAP;

            case ShaderSamplerType::Texture2DMultisample:
                return GL_TEXTURE_2D_MULTISAMPLE;

            case ShaderSamplerType::Texture1DArray:
            case ShaderSamplerType::Texture1DArrayShadow:
                return GL_TEXTURE_1D_ARRAY;

            case ShaderSamplerType::Texture2DArray:
            case ShaderSamplerType::Texture2DArrayShadow:
                return GL_TEXTURE_2D_ARRAY;

            case ShaderSamplerType::TextureCubeArray:
            case ShaderSamplerType::TextureCubeArrayShadow:
                return GL_TEXTURE_CUBE_MAP_ARRAY;

            case ShaderSamplerType::Texture2DMultisampleArray:
                return GL_TEXTURE_2D_MULTISAMPLE_ARRAY;

            default:
                return GL_TEXTURE_2D;
        }
    }

    GLMaterial::GLMaterial(std::shared_ptr<Shader> shader, std::shared_ptr<Pipeline> pipeline, bool receivesShadows, Opacity opacity)
    {
        m_Shader = shader;
        m_ReceivesShadows = receivesShadows;
        m_Opacity = opacity;
        m_Pipeline = pipeline;
        switch (m_Opacity)
        {
            case Opacity::Masked:{
                SetScalarParameter("masked", true);
                m_Pipeline->GetSpecifications().blending = false;
            }
            case Opacity::Translucent:{
                m_Pipeline->GetSpecifications().blending = true;
                m_Pipeline->GetSpecifications().srcBlend = BlendFactor::SrcAlpha;
                m_Pipeline->GetSpecifications().dstBlend = BlendFactor::OneMinusSrcAlpha;
                break;
            }
        
            case Opacity::Opaque:
            default:
            {
                m_Pipeline->GetSpecifications().blending = false;
                break;
            }
        }
    }

    void GLMaterial::SetScalarParameter(const std::string &name, const NumericValue &value)
    {
        const auto& uniforms = m_Shader->GetActiveUniformsMap();

        auto it = uniforms.find(name);

        if (it == uniforms.end())
        {
            DEBUG_LOG("Tried setting scalar parameter \'",name,"\' but it does not exist in shader : ", m_Shader->GetVertexShaderName());
            return;
        }

        m_ScalarParameters[name] = value;
    }

    void GLMaterial::SetTextureParameter(const std::string& name, uint64_t texture)
    {
        const auto& samplers = m_Shader->GetActiveSamplersMap();

        auto it = samplers.find(name);

        if (it == samplers.end())
        {
            DEBUG_LOG("Tried setting texture parameter \'",name,"\' but it does not exist in shader : ", m_Shader->GetVertexShaderName());
            return;
        }

        m_SamplersParameters[name] = (uint32_t)texture;
    }

    std::optional<NumericValue>
    GLMaterial::GetScalarParameter(const std::string& name)
    {
        auto it = m_ScalarParameters.find(name);

        if (it != m_ScalarParameters.end())
            return it->second;

        return std::nullopt;
    }

    uint64_t GLMaterial::GetTextureParameter(const std::string& name) const
    {
        auto it = m_SamplersParameters.find(name);
        if (it == m_SamplersParameters.end())
            return 0;

        return GLTexture2D::GetBindlessHandle(it->second);
    }

    uint32_t GLMaterial::GetTexturesCount() const
    {
        return (uint32_t)m_SamplersParameters.size();
    }

    bool StartsWith(const std::string& str, const std::string& prefix)
    {
        return str.rfind(prefix, 0) == 0;
    }

    uint32_t GLMaterial::GetDefaultTexture(std::string samplerName)
    {
        if(StartsWith(samplerName, "albedo")){
            return Core::GetEngine().GetResourcesManager()->GetTexture("textures/white.png")->GetHandle();
        }
        else if(StartsWith(samplerName, "roughness")){
            return Core::GetEngine().GetResourcesManager()->GetTexture("textures/white.png")->GetHandle();
        }
        else if(StartsWith(samplerName, "metallic")){
            return Core::GetEngine().GetResourcesManager()->GetTexture("textures/white.png")->GetHandle();
        }
        else if(StartsWith(samplerName, "normal")){
            return Core::GetEngine().GetResourcesManager()->GetTexture("textures/default_normal.png")->GetHandle();
        }
        else if(StartsWith(samplerName, "emissive")){
            return Core::GetEngine().GetResourcesManager()->GetTexture("textures/white.png")->GetHandle();
        }
        return 0;
    }

    bool IsGlobalSampler(const std::string& name)
    {
        return StartsWith(name,"ibl_") || StartsWith(name,"ddgi_");
    }

    void GLMaterial::Bind()
    {
        std::shared_ptr<GLShader> glShader = std::static_pointer_cast<GLShader>(m_Shader);
        glShader->Bind();

        const auto& samplers = glShader->GetActiveSamplersMap();

        for (auto& [name, samplerInfo] : samplers)
        {
            if (IsGlobalSampler(name))
                continue;

            GLuint texture = m_SamplersParameters.find(name) != m_SamplersParameters.end() ? m_SamplersParameters[name] : GetDefaultTexture(name);

            GLStateCache::BindTextureUnit(samplerInfo.binding, texture);
        }

        for (auto& [name, value] : m_ScalarParameters)
        {
            std::visit([&](auto&& v)
            {
                using T = std::decay_t<decltype(v)>;

                if constexpr (std::is_same_v<T, bool>)
                    m_Shader->SetBool(name, v);

                else if constexpr (std::is_same_v<T, int>)
                    m_Shader->SetInt(name, v);

                else if constexpr (std::is_same_v<T, float>)
                    m_Shader->SetFloat(name, v);

                else if constexpr (std::is_same_v<T, glm::vec2>)
                    m_Shader->SetVec2(name, v);

                else if constexpr (std::is_same_v<T, glm::vec3>)
                    m_Shader->SetVec3(name, v);

                else if constexpr (std::is_same_v<T, glm::vec4>)
                    m_Shader->SetVec4(name, v);

                else if constexpr (std::is_same_v<T, glm::mat2>)
                    m_Shader->SetMat2(name, v);

                else if constexpr (std::is_same_v<T, glm::mat3>)
                    m_Shader->SetMat3(name, v);

                else if constexpr (std::is_same_v<T, glm::mat4>)
                    m_Shader->SetMat4(name, v);

            }, value);
        }
    }
}
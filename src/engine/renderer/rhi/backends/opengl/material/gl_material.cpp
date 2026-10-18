#include "gl_material.hpp"
#include "engine/renderer/rhi/render_thread.hpp"

#include "engine/renderer/rhi/backends/opengl/gl_utils.hpp"
#include "engine/renderer/rhi/backends/opengl/pipeline/gl_pipeline.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/renderer/rhi/backends/opengl/shader/gl_shader.hpp"
#include "engine/renderer/rhi/backends/opengl/texture/gl_texture.hpp"

#include "engine/core/diagnostics/logger.hpp"
#include "engine/world/engine.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/assets/assetID.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"

namespace Shard::Engine::Rendering{

    [[maybe_unused]] static GLenum ShaderSamplerTypeToOpenGL(ShaderSamplerType type)
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
                break;
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
                // `masked` is a uniform of the program, which other materials sharing it set to true : an opaque one says so
                // itself, or it would keep (and discard by) whatever the last masked material left there.
                if (m_Shader->GetActiveUniformsMap().find("masked") != m_Shader->GetActiveUniformsMap().end())
                    m_ScalarParameters["masked"] = false;
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

    namespace {
        // The two textures a material falls back on for a slot it has nothing bound to. Held here, loaded once by
        // PreloadDefaultTextures() : binding a material happens on the render thread, which has no business with the
        // resources manager (the simulation uses it at the same time).
        std::shared_ptr<Texture2D> g_WhiteTexture;
        std::shared_ptr<Texture2D> g_DefaultNormalTexture;
    }

    void GLMaterial::PreloadDefaultTextures()
    {
        auto* resources = Core::GetEngine().GetResourcesManager();
        g_WhiteTexture = resources->Get<Rendering::Texture2D>(Core::Resources::AssetKind::Texture, "textures/white.png");
        g_DefaultNormalTexture = resources->Get<Rendering::Texture2D>(Core::Resources::AssetKind::Texture, "textures/default_normal.png");
    }

    uint32_t GLMaterial::GetDefaultTexture(std::string samplerName)
    {
        if(StartsWith(samplerName, "albedo") || StartsWith(samplerName, "roughness") || StartsWith(samplerName, "metallic") || StartsWith(samplerName, "emissive"))
            return g_WhiteTexture ? g_WhiteTexture->GetHandle() : 0;
        if(StartsWith(samplerName, "normal"))
            return g_DefaultNormalTexture ? g_DefaultNormalTexture->GetHandle() : 0;
        return 0;
    }

    bool IsGlobalSampler(const std::string& name)
    {
        return StartsWith(name,"ibl_") || StartsWith(name,"ddgi_") || StartsWith(name,"rp_");
    }

    void GLMaterial::Bind(std::shared_ptr<Shader> shader)
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        std::shared_ptr<Shader> target = shader ? shader : m_Shader;
        std::shared_ptr<GLShader> glShader = std::static_pointer_cast<GLShader>(target);
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
                    target->SetBool(name, v);

                else if constexpr (std::is_same_v<T, int>)
                    target->SetInt(name, v);

                else if constexpr (std::is_same_v<T, float>)
                    target->SetFloat(name, v);

                else if constexpr (std::is_same_v<T, glm::vec2>)
                    target->SetVec2(name, v);

                else if constexpr (std::is_same_v<T, glm::vec3>)
                    target->SetVec3(name, v);

                else if constexpr (std::is_same_v<T, glm::vec4>)
                    target->SetVec4(name, v);

                else if constexpr (std::is_same_v<T, glm::mat2>)
                    target->SetMat2(name, v);

                else if constexpr (std::is_same_v<T, glm::mat3>)
                    target->SetMat3(name, v);

                else if constexpr (std::is_same_v<T, glm::mat4>)
                    target->SetMat4(name, v);

            }, value);
        }
    }
}
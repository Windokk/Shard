#include "vk_material.hpp"

#include "vk_api.hpp"
#include "vk_image.hpp"
#include "vk_shader.hpp"
#include "vk_texture.hpp"

#include "engine/assets/resources_manager.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/world/engine.hpp"

namespace Shard::Engine::Rendering {

    namespace {

        bool StartsWith(const std::string& text, const std::string& prefix)
        {
            return text.rfind(prefix, 0) == 0;
        }

        // The two textures a material falls back on for a slot it has nothing bound to (see GLMaterial)
        std::shared_ptr<Texture2D> g_WhiteTexture;
        std::shared_ptr<Texture2D> g_DefaultNormalTexture;

        bool IsGlobalSampler(const std::string& name)
        {
            return StartsWith(name, "ibl_") || StartsWith(name, "ddgi_");
        }
    }

    VKMaterial::VKMaterial(std::shared_ptr<Shader> shader, std::shared_ptr<Pipeline> pipeline, bool receivesShadows, Opacity opacity)
    {
        m_Shader = shader;
        m_ReceivesShadows = receivesShadows;
        m_Opacity = opacity;
        m_Pipeline = pipeline;

        switch (m_Opacity)
        {
            case Opacity::Masked:
                SetScalarParameter("masked", true);
                m_Pipeline->GetSpecifications().blending = false;
                [[fallthrough]];
            case Opacity::Translucent:
                m_Pipeline->GetSpecifications().blending = true;
                m_Pipeline->GetSpecifications().srcBlend = BlendFactor::SrcAlpha;
                m_Pipeline->GetSpecifications().dstBlend = BlendFactor::OneMinusSrcAlpha;
                break;

            case Opacity::Opaque:
            default:
                m_Pipeline->GetSpecifications().blending = false;
                if (m_Shader->GetActiveUniformsMap().find("masked") != m_Shader->GetActiveUniformsMap().end())
                    m_ScalarParameters["masked"] = false;
                break;
        }
    }

    void VKMaterial::SetScalarParameter(const std::string& name, const NumericValue& value)
    {
        const auto& uniforms = m_Shader->GetActiveUniformsMap();
        if (uniforms.find(name) == uniforms.end())
        {
            DEBUG_LOG("Tried setting scalar parameter '", name, "' but it does not exist in shader : ", m_Shader->GetVertexShaderName());
            return;
        }

        m_ScalarParameters[name] = value;
    }

    void VKMaterial::SetTextureParameter(const std::string& name, uint64_t texture)
    {
        const auto& samplers = m_Shader->GetActiveSamplersMap();
        if (samplers.find(name) == samplers.end())
        {
            DEBUG_LOG("Tried setting texture parameter '", name, "' but it does not exist in shader : ", m_Shader->GetVertexShaderName());
            return;
        }

        m_SamplersParameters[name] = static_cast<uint32_t>(texture);
    }

    std::optional<NumericValue> VKMaterial::GetScalarParameter(const std::string& name)
    {
        auto it = m_ScalarParameters.find(name);
        if (it != m_ScalarParameters.end())
            return it->second;
        return std::nullopt;
    }

    uint64_t VKMaterial::GetTextureParameter(const std::string& name) const
    {
        auto it = m_SamplersParameters.find(name);
        if (it == m_SamplersParameters.end())
            return 0;

        // The place of the texture in the global array : 0 if it is gone
        return VK::TextureResource::Find(it->second) ? it->second : 0;
    }

    void VKMaterial::PreloadDefaultTextures()
    {
        auto* resources = Core::GetEngine().GetResourcesManager();
        g_WhiteTexture = resources->Get<Rendering::Texture2D>(Core::Resources::AssetKind::Texture, "textures/white.png");
        g_DefaultNormalTexture = resources->Get<Rendering::Texture2D>(Core::Resources::AssetKind::Texture, "textures/default_normal.png");
    }

    uint32_t VKMaterial::GetDefaultTexture(const std::string& samplerName)
    {
        if (StartsWith(samplerName, "albedo") || StartsWith(samplerName, "roughness") || StartsWith(samplerName, "metallic") || StartsWith(samplerName, "emissive"))
            return g_WhiteTexture ? g_WhiteTexture->GetHandle() : 0;
        if (StartsWith(samplerName, "normal"))
            return g_DefaultNormalTexture ? g_DefaultNormalTexture->GetHandle() : 0;
        return 0;
    }

    void VKMaterial::Bind(std::shared_ptr<Shader> shader)
    {
        std::shared_ptr<Shader> target = shader ? shader : m_Shader;
        VKRendererAPI* api = VKRendererAPI::Get();
        if (!api)
            return;

        for (const auto& [name, samplerInfo] : target->GetActiveSamplersMap())
        {
            if (IsGlobalSampler(name))
                continue;

            auto parameter = m_SamplersParameters.find(name);
            const uint32_t texture = parameter != m_SamplersParameters.end() ? parameter->second : GetDefaultTexture(name);
            api->BindTextureUnit(static_cast<uint32_t>(samplerInfo.binding), texture);
        }

        for (const auto& [name, value] : m_ScalarParameters)
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

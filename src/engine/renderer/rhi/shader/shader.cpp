#include "shader.hpp"
#include "engine/renderer/rhi/render_thread.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/backends/opengl/shader/gl_shader.hpp"
#include "engine/renderer/rhi/backends/null/null_rhi.hpp"
#include "engine/renderer/rhi/backends/vulkan/vk_shader.hpp"

#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Rendering 
{
    std::shared_ptr<Shader> Shader::GetVariant(const std::string& define)
    {
        std::lock_guard<std::mutex> lock(m_VariantMutex);

        auto it = m_Variants.find(define);
        if (it != m_Variants.end())
            return it->second;

        std::vector<std::string> defines = m_Defines;
        defines.push_back(define);

        std::shared_ptr<Shader> variant = Create(Filesystem::Path(m_VertexFilePath), Filesystem::Path(m_FragmentFilePath), Filesystem::Path(m_GeometryFilePath), defines);
        if (variant)
            variant->SetAssetID(m_AssetID);

        m_Variants[define] = variant;
        return variant;
    }

    std::shared_ptr<Shader> Shader::Create(const Filesystem::Path &vertexPath, const Filesystem::Path &fragmentPath, const Filesystem::Path &geometryPath, const std::vector<std::string>& defines)
    {
        switch (RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                if (vertexPath.GetExtensionString() == ".slang")
                {
                    DEBUG_ERROR("The shader ", vertexPath.full, " is written in Slang : it needs the Vulkan backend");
                    return nullptr;
                }
                return OnGraphicsThread([&]() -> std::shared_ptr<Shader> { return std::make_shared<GLShader>(vertexPath, fragmentPath, geometryPath, defines); });

            case RendererAPI::API::Vulkan:
                return OnGraphicsThread([&]() -> std::shared_ptr<Shader> { return std::make_shared<VKShader>(vertexPath, fragmentPath, geometryPath, defines); });

            case RendererAPI::API::Null:
                return std::make_shared<Null::NullShader>(vertexPath, fragmentPath, geometryPath, defines);

            default:
                break;

            /*case RendererAPI::API::Vulkan:
                return std::make_shared<VKShader>(vertexPath, fragmentPath, geometry);

            case RendererAPI::API::DX11:
                return std::make_shared<DX11Shader>(vertexPath, fragmentPath, geometry);

            case RendererAPI::API::DX12:
                return std::make_shared<DX12Shader>(vertexPath, fragmentPath, geometry);*/
        }

        DEBUG_ERROR("Invalid Renderer API during shader creation : ", vertexPath.full);
        return nullptr;
    }
}
#include "shader.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/backends/opengl/shader/gl_shader.hpp"

#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Rendering 
{
    std::shared_ptr<Shader> Shader::Create(const Filesystem::Path &vertexPath, const Filesystem::Path &fragmentPath, const Filesystem::Path &geometryPath)
    {
        switch (RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return std::make_shared<GLShader>(vertexPath, fragmentPath, geometryPath);

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
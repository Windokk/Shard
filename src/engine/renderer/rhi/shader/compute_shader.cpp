#include "compute_shader.hpp"
#include "engine/renderer/rhi/render_thread.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/backends/opengl/shader/gl_compute_shader.hpp"
#include "engine/renderer/rhi/backends/null/null_rhi.hpp"

#include "engine/renderer/rhi/renderer_api.hpp"

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Rendering 
{
    std::shared_ptr<ComputeShader> ComputeShader::Create(const Filesystem::Path &path)
    {
        switch (RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return OnGraphicsThread([&]() -> std::shared_ptr<ComputeShader> { return std::make_shared<GLComputeShader>(path); });

            case RendererAPI::API::Null:
                return std::make_shared<Null::NullComputeShader>(path);

            default:
                break;

            /*case RendererAPI::API::Vulkan:
                return std::make_shared<VKComputeShader>(vertexPath, fragmentPath, geometry);

            case RendererAPI::API::DX11:
                return std::make_shared<DX11ComputeShader>(vertexPath, fragmentPath, geometry);

            case RendererAPI::API::DX12:
                return std::make_shared<DX12ComputeShader>(vertexPath, fragmentPath, geometry);*/
        }

        DEBUG_ERROR("Invalid Renderer API during compute shader creation : ", path.full);
        return nullptr;
    }
}
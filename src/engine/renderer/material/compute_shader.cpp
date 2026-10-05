#include "compute_shader.hpp"

#include "engine/core/engine.hpp"

#include "engine/renderer/rhi/backends/opengl/shader/gl_compute_shader.hpp"

#include "engine/renderer/frontend/renderer.hpp"

#include "engine/debugging/logger.hpp"

namespace Shard::Engine::Rendering 
{
    std::shared_ptr<ComputeShader> ComputeShader::Create(const Filesystem::Path &path)
    {
        switch (Core::GetEngine().GetRenderer()->GetRendererAPI()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return std::make_shared<GLComputeShader>(path);

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
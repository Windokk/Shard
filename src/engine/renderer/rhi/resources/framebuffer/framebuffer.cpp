#include "framebuffer.hpp"

#include <memory>

#include "engine/world/engine.hpp"

#include "engine/renderer/frontend/renderer.hpp"

#include "engine/renderer/rhi/backends/opengl/framebuffer/gl_framebuffer.hpp"

namespace Shard::Engine::Rendering{
    std::shared_ptr<Framebuffer> Framebuffer::Create(const FramebufferSpecifications& specs)
    {
        switch(Core::GetEngine().GetRenderer()->GetRendererAPI()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return std::make_shared<GLFramebuffer>(specs);

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
}
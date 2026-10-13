#include "framebuffer.hpp"
#include "engine/renderer/rhi/render_thread.hpp"

#include <memory>

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/renderer_api.hpp"

#include "engine/renderer/rhi/backends/opengl/framebuffer/gl_framebuffer.hpp"
#include "engine/renderer/rhi/backends/null/null_rhi.hpp"

namespace Shard::Engine::Rendering{
    std::shared_ptr<Framebuffer> Framebuffer::Create(const FramebufferSpecifications& specs)
    {
        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return OnGraphicsThread([&]() -> std::shared_ptr<Framebuffer> { return std::make_shared<GLFramebuffer>(specs); });

            case RendererAPI::API::Null:
                return std::make_shared<Null::NullFramebuffer>(specs);

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
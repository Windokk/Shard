#include "texture_array.hpp"
#include "engine/renderer/rhi/render_thread.hpp"

#include "engine/renderer/rhi/backends/opengl/texture/gl_texture_array.hpp"
#include "engine/renderer/rhi/backends/null/null_rhi.hpp"
#include "engine/renderer/rhi/backends/vulkan/vk_texture_array.hpp"

#include "engine/renderer/rhi/renderer_api.hpp"

namespace Shard::Engine::Rendering{

    std::shared_ptr<Texture2DArray> Texture2DArray::Create(const TextureSpecifications& specs, uint32_t layers)
    {
        if (layers == 0)
            return nullptr;

        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return OnGraphicsThread([&]() -> std::shared_ptr<Texture2DArray> { return std::make_shared<GLTexture2DArray>(specs, layers); });

            case RendererAPI::API::Vulkan:
                return OnGraphicsThread([&]() -> std::shared_ptr<Texture2DArray> { return std::make_shared<VKTexture2DArray>(specs, layers); });

            case RendererAPI::API::Null:
                return std::make_shared<Null::NullTexture2DArray>(specs, layers);

            default:
                return nullptr;
        }
    }
}

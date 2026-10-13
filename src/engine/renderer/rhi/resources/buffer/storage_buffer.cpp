#include "storage_buffer.hpp"
#include "engine/renderer/rhi/render_thread.hpp"

#include "engine/world/engine.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"

#include "engine/renderer/rhi/backends/opengl/buffer/gl_storage_buffer.hpp"
#include "engine/renderer/rhi/backends/null/null_rhi.hpp"

namespace Shard::Engine::Rendering{

    std::shared_ptr<StorageBuffer> StorageBuffer::Create(uint32_t size)
    {
        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return OnGraphicsThread([&]() -> std::shared_ptr<StorageBuffer> { return std::make_shared<GLStorageBuffer>(size); });

            case RendererAPI::API::Null:
                return std::make_shared<Null::NullStorageBuffer>(size);

            default:
                break;
        }

        return nullptr;
    }
}
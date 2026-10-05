#include "storage_buffer.hpp"

#include "engine/world/engine.hpp"
#include "engine/renderer/frontend/renderer.hpp"

#include "engine/renderer/rhi/backends/opengl/buffer/gl_storage_buffer.hpp"

namespace Shard::Engine::Rendering{

    std::shared_ptr<StorageBuffer> StorageBuffer::Create(uint32_t size)
    {
        switch(Core::GetEngine().GetRenderer()->GetRendererAPI()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return std::make_shared<GLStorageBuffer>(size);
        }

        return nullptr;
    }
}
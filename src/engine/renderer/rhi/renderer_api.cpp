#include "renderer_api.hpp"

#include "engine/renderer/rhi/backends/opengl/renderer_api/gl_api.hpp"

#include "engine/world/engine.hpp"

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Rendering{

    std::shared_ptr<RendererAPI> RendererAPI::Create(API api)
    {
        switch(api)
        {
            case API::OpenGL: return std::make_shared<GLRendererAPI>();
            default: return nullptr;
        }
    }
}
#include "renderer_api.hpp"

#include "engine/renderer/rhi/backends/opengl/renderer_api/gl_api.hpp"
#include "engine/renderer/rhi/backends/null/null_rhi.hpp"
#include "engine/renderer/rhi/backends/vulkan/vk_api.hpp"

#include "engine/world/engine.hpp"

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Rendering{

    namespace {
        RendererAPI* g_CurrentAPI = nullptr;
        WindowHooks g_WindowHooks;
    }

    DebugScope::DebugScope(RendererAPI* api, const char* name) : m_Api(api)
    {
        if (m_Api)
            m_Api->PushDebugGroup(name);
    }

    DebugScope::~DebugScope()
    {
        if (m_Api)
            m_Api->PopDebugGroup();
    }

    void RendererAPI::SetWindowHooks(const WindowHooks& hooks)
    {
        g_WindowHooks = hooks;
    }

    const WindowHooks& RendererAPI::GetWindowHooks()
    {
        return g_WindowHooks;
    }

    RendererAPI* RendererAPI::Current()
    {
        return g_CurrentAPI;
    }

    void RendererAPI::SetCurrent(RendererAPI* api)
    {
        g_CurrentAPI = api;
    }

    std::shared_ptr<RendererAPI> RendererAPI::Create(API api)
    {
        switch(api)
        {
            case API::OpenGL: return std::make_shared<GLRendererAPI>();
            case API::Null: return std::make_shared<Null::NullRendererAPI>();
            case API::Vulkan: {
                auto vulkan = std::make_shared<VKRendererAPI>();
                if (!vulkan->Init())
                    return nullptr;
                return vulkan;
            }
            default: return nullptr;
        }
    }
}
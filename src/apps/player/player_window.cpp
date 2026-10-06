#include "player_window.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/frontend/renderer.hpp"

#include "engine/renderer/rhi/backends/glad/include/glad/gl.h"
#include "engine/renderer/rhi/backends/glad/include/glad/vulkan.h"

namespace Shard::Game::Core::Platform{

    void PlayerWindow::Init(const std::string &title, const int &width, const int &height, const bool &fullscreen, const int &vsync, const uint32_t& api)
    {
        SDLWindow::Init(title, width, height, fullscreen, vsync, api);

        // The framebuffers follow the size of the window
        AddEventListener([](const Engine::Core::Platform::OSEvent& event) {
            if(event.type == Engine::Core::Platform::OSEventType::WindowResized && event.width > 0 && event.height > 0)
                Engine::Core::GetEngine().GetRenderer()->RescaleFramebuffers(event.width, event.height);
        });

        if(api == (uint32_t)Engine::Rendering::RendererAPI::API::OpenGL)
            gladLoadGL((GLADloadfunc)SDLWindow::GLProcLoader);
        else if(api == (uint32_t)Engine::Rendering::RendererAPI::API::Vulkan)
        {
            //gladLoadVulkan(Engine::Core::GetEngine().GetRenderer()->GetDevicePointer?,(GLADloadfunc)SDLWindow::GLProcLoader);
        }
    }

    void PlayerWindow::SwapBuffers()
    {
        Engine::Core::GetEngine().GetRenderer()->PresentToScreen(GetFramebufferWidth(), GetFramebufferHeight());

        SDLWindow::SwapBuffers();
    }

    Shard::Engine::Core::Platform::SystemInfos PlayerWindow::GetSystemInfos() const
    {
        Engine::Core::Platform::SystemInfos ret = SDLWindow::GetSystemInfos();

        ret.gpu_vendor   = Engine::Core::GetEngine().GetRenderer()->GetDeviceVendor();
        ret.gpu_renderer = Engine::Core::GetEngine().GetRenderer()->GetRendererName();
        ret.gl_version   = Engine::Core::GetEngine().GetRenderer()->GetDriverVersion();

        return ret;
    }
}

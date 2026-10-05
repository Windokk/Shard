#include "glfw_window.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/frontend/renderer.hpp"

#include "engine/renderer/rhi/backends/glad/include/glad/gl.h"
#include "engine/renderer/rhi/backends/glad/include/glad/vulkan.h"

namespace Shard::Game::Core::Platform{
    
    void Platform::GLFWWindow::Init(const std::string &title, const int &width, const int &height, const bool &fullscreen, const int &vsync, const uint32_t& api)
    {
        glfwInit();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_SAMPLES, 4);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        if(fullscreen){
            GLFWmonitor* primary = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(primary);
            window = glfwCreateWindow(mode->width, mode->height, title.c_str(), primary, nullptr);
        }
        else{
            window = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
        }
        
        if (window == NULL)
        {
            glfwTerminate();
            DEBUG_FATAL("Failed to create GLFW window");
        }

        glfwMakeContextCurrent(window);
        glfwSetWindowUserPointer(window, this);
        glfwSetWindowSizeCallback(window, [](GLFWwindow* window, int width, int height) {
            Engine::Core::GetEngine().GetRenderer()->RescaleFramebuffers(width, height);
        });
        glfwSwapInterval(vsync);

        if(api == (int)Engine::Rendering::RendererAPI::API::OpenGL)
            gladLoadGL((GLADloadfunc)glfwGetProcAddress);
        else if(api == (int)Engine::Rendering::RendererAPI::API::Vulkan)
        {    
            //gladLoadVulkan(Engine::Core::GetEngine().GetRenderer()->GetDevicePointer?,(GLADloadfunc)glfwGetProcAddress);
        }
    }

    void Platform::GLFWWindow::SetGLFWInputManager(GLFWInput *inputManager)
    {
        this->inputManager = inputManager;
    }

    void Platform::GLFWWindow::SetTitle(const std::string &title)
    {
        glfwSetWindowTitle(window, title.c_str());
    }

    void Platform::GLFWWindow::PollEvents()
    {
        glfwPollEvents();
    }

    void Platform::GLFWWindow::SwapBuffers()
    {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        Engine::Core::GetEngine().GetRenderer()->PresentToScreen(width, height);

        glfwSwapBuffers(window);
    }

    bool Platform::GLFWWindow::ShouldClose() const
    {
        return glfwWindowShouldClose(window);
    }

    int Platform::GLFWWindow::GetFramebufferWidth() const
    {
        int width;
        glfwGetFramebufferSize(window, &width, nullptr);
        return width;
    }

    int Platform::GLFWWindow::GetFramebufferHeight() const
    {
        int height;
        glfwGetFramebufferSize(window, nullptr, &height);
        return height;
    }

    void *Platform::GLFWWindow::GetNativeHandle() const
    {
        return static_cast<void*>(window);
    }

    void Platform::GLFWWindow::Destroy() const
    {
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void Platform::GLFWWindow::ToggleFullscreen()
    {
        const bool fullscreen = glfwGetWindowMonitor(window) != nullptr;
        if(fullscreen) {
            // Restore the window position and size.
            glfwSetWindowMonitor(window, nullptr, windowPosX, windowPosY, windowWidth, windowHeight, 0);
            // Check the window position and size (if we are on a screen smaller than the initial size).
            glfwGetWindowPos(window, &windowPosX, &windowPosY);
            glfwGetWindowSize(window, &windowWidth, &windowHeight);
            Engine::Core::GetEngine().GetRenderer()->RescaleFramebuffers(windowWidth, windowHeight);
        } else {
            // Backup the window current frame.
            glfwGetWindowPos(window, &windowPosX, &windowPosY);
            glfwGetWindowSize(window, &windowWidth, &windowHeight);
            // Move to fullscreen on the primary monitor.
            GLFWmonitor * monitor	= glfwGetPrimaryMonitor();
            const GLFWvidmode * mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            Engine::Core::GetEngine().GetRenderer()->RescaleFramebuffers(mode->width, mode->height);
        }
    }

    void Platform::GLFWWindow::ProcessInputs() const
    {
        
    }

    int Platform::GLFWWindow::GetBytesPerPixel() const
    {
        int redBits = glfwGetWindowAttrib(window, GLFW_RED_BITS);
        int greenBits = glfwGetWindowAttrib(window, GLFW_GREEN_BITS);
        int blueBits = glfwGetWindowAttrib(window, GLFW_BLUE_BITS);
        int alphaBits = glfwGetWindowAttrib(window, GLFW_ALPHA_BITS);

        return (redBits + greenBits + blueBits + alphaBits) / 8;
    }

    Shard::Engine::Core::Platform::SystemInfos Shard::Game::Core::Platform::GLFWWindow::GetSystemInfos() const
    {
        Engine::Core::Platform::SystemInfos ret{};

        ret.gpu_vendor   = Engine::Core::GetEngine().GetRenderer()->GetDeviceVendor();
        ret.gpu_renderer = Engine::Core::GetEngine().GetRenderer()->GetRendererName();
        ret.gl_version   = Engine::Core::GetEngine().GetRenderer()->GetDriverVersion();

        // GLFW context version
        int major, minor, rev;
        glfwGetVersion(&major, &minor, &rev);
        ret.windowHostVersion = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(rev);

        // Monitor and resolution
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        ret.connectedMonitorsCount = count;

        for (int i = 0; i < count; ++i) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            ret.monitors.push_back({mode->width, mode->height, mode->refreshRate});
        }
        
        ret.windowHost = Engine::Core::Platform::GLFW;

        return ret;
    }
}


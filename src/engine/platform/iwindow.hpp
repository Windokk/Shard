#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Shard::Engine::Core::Platform {

    struct MonitorInfos{
        int width, height, refreshRate;
    };

    enum WindowHost{
        GLFW,
        QT
    };

    struct SystemInfos{
        std::string gpu_vendor;
        std::string gpu_renderer;
        std::string gl_version;
        int connectedMonitorsCount;
        std::vector<MonitorInfos> monitors;
        WindowHost windowHost;
        std::string windowHostVersion;
    };
    
    class IWindow {
    public:
        virtual ~IWindow() = default;

        virtual void Init(const std::string& title, const int& width, const int& height, 
                            const bool& fullscreen, const int& vsync, const uint32_t& api) = 0;

        virtual void* GetNativeHandle() const = 0;
        virtual void PollEvents() = 0;
        virtual bool ShouldClose() const = 0;

        virtual void SwapBuffers() = 0;

        // Renders a single minimal "loading" frame (progress in [0,1]) and presents it - called
        // repeatedly by a blocking level load (e.g. the boot-time default level) so the window stays
        // responsive and shows feedback instead of appearing frozen. No-op by default; only
        // implementations with a UI system available.
        virtual void DrawLoadingFrame(float progress) {}
        virtual int GetFramebufferWidth() const = 0;
        virtual int GetFramebufferHeight() const = 0;
        virtual int GetBytesPerPixel() const = 0;

        virtual void SetTitle(const std::string& title) = 0;

        virtual void ToggleFullscreen() = 0;

        virtual void Destroy() const = 0;

        virtual void ProcessInputs() const = 0;

        virtual SystemInfos GetSystemInfos() const = 0;
    };

}
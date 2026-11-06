#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "engine/platform/events/os_event.hpp"

namespace Shard::Engine::Core::Platform {

    struct MonitorInfos{
        int width = 0, height = 0, refreshRate = 0;
        int x = 0, y = 0;
        float contentScale = 1.0f;
        bool hdr = false;
        bool primary = false;
        std::string name;
    };

    enum WindowHost{
        SDL3,
        QT,
        Headless
    };

    struct SystemInfos{
        std::string gpu_vendor;
        std::string gpu_renderer;
        std::string driver_version;
        int connectedMonitorsCount = 0;
        std::vector<MonitorInfos> monitors;
        WindowHost windowHost = SDL3;
        std::string windowHostVersion;

        std::string cpuBrand;
        int cpuLogicalCores = 0;
        uint64_t totalRamMB = 0;
    };

    class IWindow : public OSEventSource {
    public:
        virtual ~IWindow() = default;

        virtual void Init(const std::string& title, const int& width, const int& height, 
                            const bool& fullscreen, const int& vsync, const uint32_t& api) = 0;

        virtual void* GetNativeHandle() const = 0;
        virtual void PollEvents() = 0;
        virtual bool ShouldClose() const = 0;

        // Called after Extract publishes the snapshot, before handing off to the render thread. From SwapBuffers overlay draws would land one frame late.
        virtual void PrepareFrame() {}

        virtual void SwapBuffers() = 0;

        virtual void SetVSync(bool enabled) { (void)enabled; }

        // One thread owns the context at a time. It moves to the render thread when that starts and back when it stops.
        virtual bool MakeContextCurrent(bool current) { (void)current; return true; }

        virtual void DrawLoadingFrame(float progress) {}

        virtual std::vector<std::string> GetVulkanInstanceExtensions() const { return {}; }

        virtual uint64_t CreateVulkanSurface(void* vulkanInstance) const { (void)vulkanInstance; return 0; }

        virtual int GetFramebufferWidth() const = 0;
        virtual int GetFramebufferHeight() const = 0;
        virtual int GetBytesPerPixel() const = 0;

        virtual void SetTitle(const std::string& title) = 0;

        virtual void ToggleFullscreen() = 0;

        virtual void Destroy() const = 0;

        virtual void ProcessInputs() const = 0;

        virtual SystemInfos GetSystemInfos() const = 0;

        virtual std::vector<MonitorInfos> GetMonitors() const { return {}; }

        virtual float GetContentScale() const { return 1.0f; }

        // 0 arrow, 1 text, 2 hand, 3 crosshair, 4 resize E-W, 5 resize N-S, 6 resize NW-SE, 7 resize NE-SW, 8 move, 9 not allowed.
        virtual void SetCursorShape(int shape) { (void)shape; }

        virtual void SetTextInputActive(bool active, int x, int y, int width, int height) { (void)active; (void)x; (void)y; (void)width; (void)height; }
    };

}

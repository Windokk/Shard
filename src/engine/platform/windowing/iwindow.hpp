#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "engine/platform/events/os_event.hpp"

namespace Shard::Engine::Core::Platform {

    struct MonitorInfos{
        int width = 0, height = 0, refreshRate = 0;
        int x = 0, y = 0;               // position of the monitor in the desktop space
        float contentScale = 1.0f;      // DPI scale (1.0 = 96 dpi)
        bool hdr = false;
        bool primary = false;
        std::string name;
    };

    enum WindowHost{
        SDL3,
        QT
    };

    struct SystemInfos{
        std::string gpu_vendor;
        std::string gpu_renderer;
        std::string gl_version;
        int connectedMonitorsCount = 0;
        std::vector<MonitorInfos> monitors;
        WindowHost windowHost = SDL3;
        std::string windowHostVersion;

        // CPU / RAM (platform/hardware)
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
        /// @brief Pumps the OS events : they are dispatched to the listeners (see OSEventSource).
        virtual void PollEvents() = 0;
        virtual bool ShouldClose() const = 0;

        virtual void SwapBuffers() = 0;

        // Renders a single minimal "loading" frame (progress in [0,1]) and presents it - called
        // repeatedly by a blocking world load (e.g. the boot-time default world) so the window stays
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

        /// @brief Every monitor connected to the machine.
        virtual std::vector<MonitorInfos> GetMonitors() const { return {}; }

        /// @brief DPI scale of the monitor the window is on (1.0 = 96 dpi).
        virtual float GetContentScale() const { return 1.0f; }
    };

}

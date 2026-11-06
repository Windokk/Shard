#pragma once
#include <atomic>

#include "engine/platform/windowing/iwindow.hpp"

#include <SDL3/SDL.h>

#include <deque>

namespace Shard::Engine::Core::Platform {

    class SDLWindow : public IWindow {
    public:
        void Init(const std::string& title, const int& width, const int& height,
                    const bool& fullscreen, const int& vsync, const uint32_t& api) override;

        void* GetNativeHandle() const override { return m_Window; }
        void PollEvents() override;
        bool ShouldClose() const override { return m_ShouldClose; }

        void SwapBuffers() override;
        void SetVSync(bool enabled) override;
        bool MakeContextCurrent(bool current) override;

        int GetFramebufferWidth() const override;
        int GetFramebufferHeight() const override;
        int GetBytesPerPixel() const override;

        void SetTitle(const std::string& title) override;
        void ToggleFullscreen() override;
        void Destroy() const override;
        void ProcessInputs() const override {}

        SystemInfos GetSystemInfos() const override;
        std::vector<MonitorInfos> GetMonitors() const override;
        float GetContentScale() const override;
        void SetCursorShape(int shape) override;
        void SetTextInputActive(bool active, int x, int y, int width, int height) override;

        void RequestClose() { m_ShouldClose = true; }
        void CancelClose() { m_ShouldClose = false; }

        void PostEvent(const OSEvent& event) { m_Posted.push_back(event); }

        void SetOSInputSuppressed(bool suppressed) { m_SuppressOSInput = suppressed; }

        SDL_Window* GetSDLWindow() const { return m_Window; }
        SDL_GLContext GetGLContext() const { return m_GLContext; }

        std::vector<std::string> GetVulkanInstanceExtensions() const override;
        uint64_t CreateVulkanSurface(void* vulkanInstance) const override;

        static void* GLProcLoader(const char* name);

    protected:
        virtual void OnNativeEvent(const SDL_Event& event) {}

    private:
        void Translate(const SDL_Event& native);

        SDL_Window* m_Window = nullptr;
        SDL_GLContext m_GLContext = nullptr;
        bool m_Vulkan = false;
        bool m_ShouldClose = false;
        std::deque<OSEvent> m_Posted;
        bool m_SuppressOSInput = false;
        std::atomic<int> m_SwapInterval{0};
        std::atomic<int> m_RequestedSwapInterval{0};
    };
}

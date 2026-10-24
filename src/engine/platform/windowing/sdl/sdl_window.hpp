#pragma once
#include <atomic>

#include "engine/platform/windowing/iwindow.hpp"

#include <SDL3/SDL.h>

#include <deque>

namespace Shard::Engine::Core::Platform {

    /// @brief SDL3 window with an OpenGL context : everything the window systems have in common (context, events, fullscreen,
    /// monitors, DPI). The editor and the player derive from it for what they draw.
    ///
    /// SDL events are translated to OSEvent and sent to the listeners (the input manager is one of them).
    /// A derived class can see the raw SDL event first (to feed ImGui for instance) with OnNativeEvent.
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

        /// @brief Asks the application to close (ShouldClose becomes true), like the close button would.
        void RequestClose() { m_ShouldClose = true; }
        /// @brief Drops a close that was asked for (the application asked the user first, who changed their mind).
        void CancelClose() { m_ShouldClose = false; }

        /// @brief Queues an event the window did not get from the OS (a replay playing back) : it is dispatched by the next
        /// PollEvents, after what the OS sent, like an event that came from the keyboard.
        void PostEvent(const OSEvent& event) { m_Posted.push_back(event); }

        /// @brief While set, the keyboard, mouse and gamepad events of the OS are dropped : a replay is playing, and what the
        /// person at the keyboard does must not mix into it. Window events (close, resize) still go through.
        void SetOSInputSuppressed(bool suppressed) { m_SuppressOSInput = suppressed; }

        SDL_Window* GetSDLWindow() const { return m_Window; }
        SDL_GLContext GetGLContext() const { return m_GLContext; }

        std::vector<std::string> GetVulkanInstanceExtensions() const override;
        uint64_t CreateVulkanSurface(void* vulkanInstance) const override;

        /// @brief Function loader for the GL function pointers : gladLoadGL((GLADloadfunc)SDLWindow::GLProcLoader)
        static void* GLProcLoader(const char* name);

    protected:
        /// @brief Called with every SDL event before it is translated and dispatched.
        virtual void OnNativeEvent(const SDL_Event& event) {}

    private:
        void Translate(const SDL_Event& native);

        SDL_Window* m_Window = nullptr;
        SDL_GLContext m_GLContext = nullptr;
        bool m_Vulkan = false;      // the window is a Vulkan one : no OpenGL context, the renderer presents
        bool m_ShouldClose = false;
        std::deque<OSEvent> m_Posted;
        bool m_SuppressOSInput = false;
        std::atomic<int> m_SwapInterval{0};          // the vertical sync the context has, set again whenever the context moves
        std::atomic<int> m_RequestedSwapInterval{0}; // the one asked for (SetVSync), taken by the thread that swaps
    };
}

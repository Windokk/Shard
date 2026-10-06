#pragma once

#include "engine/platform/windowing/iwindow.hpp"

#include <SDL3/SDL.h>

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

        /// @brief Asks the application to close (ShouldClose becomes true), like the close button would.
        void RequestClose() { m_ShouldClose = true; }

        SDL_Window* GetSDLWindow() const { return m_Window; }
        SDL_GLContext GetGLContext() const { return m_GLContext; }

        /// @brief Function loader for the GL function pointers : gladLoadGL((GLADloadfunc)SDLWindow::GLProcLoader)
        static void* GLProcLoader(const char* name);

    protected:
        /// @brief Called with every SDL event before it is translated and dispatched.
        virtual void OnNativeEvent(const SDL_Event& event) {}

    private:
        void Translate(const SDL_Event& native);

        SDL_Window* m_Window = nullptr;
        SDL_GLContext m_GLContext = nullptr;
        bool m_ShouldClose = false;
    };
}

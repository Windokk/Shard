#pragma once

#include "engine/platform/windowing/iplatform.hpp"
#include "engine/platform/windowing/sdl/sdl_window.hpp"
#include "engine/platform/devices/sdl/sdl_input.hpp"
#include "engine/platform/clipboard/clipboard.hpp"

#include <memory>
#include <type_traits>

#ifdef CreateWindow
#undef CreateWindow
#endif

namespace Shard::Engine::Core::Platform {

    // SDL3 platform, parameterised on the window class so that the editor (ImGui main window) and the
    // player (plain window) share the same input handling.
    template <class WindowT>
    class SDLPlatform : public IPlatform {
        static_assert(std::is_base_of_v<SDLWindow, WindowT>, "WindowT must derive from SDLWindow");
        public:
            IWindow* GetWindow() override { return window.get(); }
            IInput* GetInput() override { return input.get(); }

            void CreateWindow(const std::string& title, const int& width, const int& height,
                            const bool& fullscreen, const int& vsync, const uint32_t& api) override
            {
                window = std::make_unique<WindowT>();
                window->Init(title, width, height, fullscreen, vsync, api);
            }

            void CreateInput() override {
                input = std::make_unique<SDLInput>(window.get());
                input->Init();
            }

            // Utility
            void SetClipboardText(const std::string& text) override { Clipboard::SetText(text); }
            std::string GetClipboardText() override { return Clipboard::GetText(); }
        private:
            std::unique_ptr<WindowT> window;
            std::unique_ptr<SDLInput> input;
    };
}

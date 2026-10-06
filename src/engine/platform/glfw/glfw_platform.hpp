#pragma once

#include "engine/platform/iplatform.hpp"
#include "engine/platform/glfw/glfw_input.hpp"
#include "engine/platform/glfw/glfw_window_base.hpp"

#include <memory>
#include <type_traits>

#ifdef CreateWindow
#undef CreateWindow
#endif

namespace Shard::Engine::Core::Platform {

    // GLFW platform, parameterised on the window class so that the editor (ImGui main window) and the
    // player (plain window) share the same input handling.
    template <class WindowT>
    class GLFWPlatform : public IPlatform {
        static_assert(std::is_base_of_v<GLFWWindowBase, WindowT>, "WindowT must derive from GLFWWindowBase");
        public:
            IWindow* GetWindow() override { return window.get(); };
            IInput* GetInput() override { return input.get(); };

            void CreateWindow(const std::string& title, const int& width, const int& height,
                            const bool& fullscreen, const int& vsync, const uint32_t& api) override
            {
                window = std::make_unique<WindowT>();
                window->Init(title, width, height, fullscreen, vsync, api);
            }

            void CreateInput() override {
                auto inputManager = std::make_unique<GLFWInput>();
                inputManager->SetWindow(static_cast<GLFWwindow*>(window->GetNativeHandle()));
                inputManager->Init();

                input = std::move(inputManager);
                auto* glfwWindow = dynamic_cast<GLFWWindowBase*>(window.get());
                if (glfwWindow) {
                    glfwWindow->SetGLFWInputManager(static_cast<GLFWInput*>(input.get()));
                }
            }

            // Utility
            void SetClipboardText(const std::string& text) override { }
            std::string GetClipboardText() override { return ""; }
        private:
            std::unique_ptr<IWindow> window;
            std::unique_ptr<IInput> input;
    };
}

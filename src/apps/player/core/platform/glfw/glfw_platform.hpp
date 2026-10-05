#pragma once

#include "engine/platform/iplatform.hpp"

#include <memory>

#include "glfw_window.hpp"

#ifdef CreateWindow
#undef CreateWindow
#endif

namespace Shard::Game::Core::Platform {

    class GLFWPlatform : public Engine::Core::Platform::IPlatform {
        public:
            Engine::Core::Platform::IWindow* GetWindow() override { return window.get(); };
            Engine::Core::Platform::IInput* GetInput() override { return input.get(); };

            void CreateWindow(const std::string& title, const int& width, const int& height, 
                            const bool& fullscreen, const int& vsync, const uint32_t& api) override 
            {
                window = std::make_unique<GLFWWindow>();
                window->Init(title, width, height, fullscreen, vsync, api);
            }

            void CreateInput() override {
                auto inputManager = std::make_unique<GLFWInput>();
                inputManager->SetWindow(static_cast<GLFWwindow*>(window->GetNativeHandle()));
                inputManager->Init();

                input = std::move(inputManager);
                auto glfwWindow = dynamic_cast<GLFWWindow*>(window.get());
                if (glfwWindow) {
                    glfwWindow->SetGLFWInputManager(static_cast<GLFWInput*>(input.get()));
                }
            }

            // Utility
            void SetClipboardText(const std::string& text) override { }
            std::string GetClipboardText() override { return ""; }
        private:
            std::unique_ptr<Engine::Core::Platform::IWindow> window;
            std::unique_ptr<Engine::Core::Platform::IInput> input;
    };
}
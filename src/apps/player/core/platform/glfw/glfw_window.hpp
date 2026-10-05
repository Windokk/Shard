#pragma once

#include "engine/platform/iwindow.hpp"

#include "engine/renderer/frontend/renderer.hpp"

#include "engine/debugging/logger.hpp"

#include "glfw_input.hpp"

namespace Shard::Game::Core::Platform {

    class GLFWWindow : public Engine::Core::Platform::IWindow {
    public:
        void Init(const std::string& title, const int& width, const int& height, 
                    const bool& fullscreen, const int& vsync, const uint32_t& api) override;

        void SetGLFWInputManager(GLFWInput* inputManager);

        void SetTitle(const std::string& title) override;

        void PollEvents() override;

        void SwapBuffers() override;

        bool ShouldClose() const override;

        int GetFramebufferWidth() const override;

        int GetFramebufferHeight() const override;

        void* GetNativeHandle() const override;

        void Destroy() const override;
  
        void ToggleFullscreen() override;

        void ProcessInputs() const override;

        int GetBytesPerPixel() const override;

        Engine::Core::Platform::SystemInfos GetSystemInfos() const override;

        GLFWInput* inputManager;

    private:
        GLFWwindow* window = nullptr;

        int windowPosX = 0;
        int windowPosY = 0;
        int windowWidth = 0;
        int windowHeight = 0;
    };
}
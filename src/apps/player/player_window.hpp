#pragma once

#include "engine/platform/windowing/sdl/sdl_window.hpp"

#include "engine/renderer/frontend/renderer.hpp"

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Game::Core::Platform {

    /// @brief The player's window : the SDL window of the platform layer, which presents the renderer's frame.
    class PlayerWindow : public Engine::Core::Platform::SDLWindow {
    public:
        void Init(const std::string& title, const int& width, const int& height,
                    const bool& fullscreen, const int& vsync, const uint32_t& api) override;

        void SwapBuffers() override;

        Engine::Core::Platform::SystemInfos GetSystemInfos() const override;
    };
}

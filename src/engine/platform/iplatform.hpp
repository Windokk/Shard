#pragma once

#include "iwindow.hpp"
#include "iinput.hpp"

#ifdef CreateWindow
#undef CreateWindow
#endif

namespace Shard::Engine::Core::Platform {

    class IPlatform {
        public:
            virtual ~IPlatform() = default;

            // Core platform components
            virtual IWindow* GetWindow() = 0;
            virtual IInput* GetInput() = 0;

            virtual void CreateWindow(const std::string& title, const int& width, const int& height, 
                            const bool& fullscreen, const int& vsync, const uint32_t& api) = 0;
            virtual void CreateInput() = 0;

            // Utility
            virtual void SetClipboardText(const std::string& text) = 0;
            virtual std::string GetClipboardText() = 0;
    };
}
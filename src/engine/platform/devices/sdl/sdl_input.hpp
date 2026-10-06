#pragma once

#include <array>
#include <vector>

#include "engine/platform/devices/iinput.hpp"
#include "engine/platform/windowing/sdl/sdl_window.hpp"

namespace Shard::Engine::Core::Platform
{
    /// @brief Keyboard and mouse state built from the OS events of an SDLWindow.
    class SDLInput : public IInput {
        public:
            explicit SDLInput(SDLWindow* window);
            ~SDLInput() override;

            void Init() override;
            void Tick() override;
            void Shutdown() override;

            // Keyboard
            bool IsKeyDown(Input::Key key) const override;
            bool IsKeyUp(Input::Key key) const override;
            bool WasKeyPressed(Input::Key key) const override;
            bool WasKeyReleased(Input::Key key) const override;

            // Mouse
            bool IsMouseDown(Input::MouseButton button) const override;
            bool IsMouseUp(Input::MouseButton button) const override;
            bool WasMousePressed(Input::MouseButton button) const override;
            bool WasMouseReleased(Input::MouseButton button) const override;

            void SetCursorVisibility(CursorVisibility visibility) const override;
            void GetCursorPos(double* x, double* y) const override;
            void SetCursorPos(double x, double y) const override;

            // Gamepads
            int GetGamepadCount() const override { return static_cast<int>(m_Pads.size()); }
            std::string GetGamepadName(int pad) const override;
            bool IsGamepadButtonDown(int pad, Input::GamepadButton button) const override;
            bool WasGamepadButtonPressed(int pad, Input::GamepadButton button) const override;
            bool WasGamepadButtonReleased(int pad, Input::GamepadButton button) const override;
            float GetGamepadAxis(int pad, Input::GamepadAxis axis, float deadZone = 0.1f) const override;
            bool SetGamepadRumble(int pad, float lowFrequency, float highFrequency, int durationMs) override;

        private:
            static constexpr size_t kKeyCount = static_cast<size_t>(Input::Key::Menu) + 1;
            static constexpr size_t kMouseButtonCount = static_cast<size_t>(Input::MouseButton::Button7) + 1;

            static constexpr size_t kPadButtonCount = static_cast<size_t>(Input::GamepadButton::Count);
            static constexpr size_t kPadAxisCount = static_cast<size_t>(Input::GamepadAxis::Count);

            struct Pad {
                SDL_JoystickID id = 0;
                SDL_Gamepad* handle = nullptr;
                std::array<bool, kPadButtonCount> buttons{};
                std::array<bool, kPadButtonCount> previousButtons{};
                std::array<float, kPadAxisCount> axes{};
            };

            void OnEvent(const OSEvent& event);
            void Clear();

            void OpenGamepad(SDL_JoystickID id);
            void CloseGamepad(SDL_JoystickID id);
            Pad* FindPad(uint32_t id);
            const Pad* GetPad(int index) const;

            SDLWindow* m_Window = nullptr;
            OSEventSource::ListenerId m_Listener = 0;
            bool m_Listening = false;

            std::array<bool, kKeyCount> m_Keys{};
            std::array<bool, kKeyCount> m_PreviousKeys{};
            std::array<bool, kMouseButtonCount> m_Buttons{};
            std::array<bool, kMouseButtonCount> m_PreviousButtons{};

            std::vector<Pad> m_Pads;    // in the order they were plugged in

            // Virtual cursor position : unbounded while the cursor is captured (disabled), like a GLFW disabled cursor.
            mutable double m_CursorX = 0.0;
            mutable double m_CursorY = 0.0;
            mutable bool m_Captured = false;
    };
}

#pragma once

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "engine/platform/devices/iinput.hpp"
#include "engine/platform/windowing/iplatform.hpp"
#include "engine/platform/windowing/iwindow.hpp"

#ifdef CreateWindow
#undef CreateWindow
#endif

namespace Shard::Engine::Core::Platform {

    /// @brief A window that is not on any screen : no window system, no graphics context, no SDL. It has a size (the
    /// viewport the renderer lays its targets out for) and an event queue, which is how input reaches the engine in a run
    /// nobody is sitting in front of : whoever drives the run (a replay, a test) posts OS events, the engine's input phase
    /// reads them with PollEvents() exactly as it would from a real window.
    class HeadlessWindow : public IWindow {
    public:
        void Init(const std::string& title, const int& width, const int& height,
                  const bool& fullscreen, const int& vsync, const uint32_t& api) override;

        /// @brief Queues an event for the next PollEvents()
        void PostEvent(const OSEvent& event);

        /// @brief Asks the run to end, like closing a window would
        void RequestClose() { m_CloseRequested = true; }

        void* GetNativeHandle() const override { return nullptr; }
        void PollEvents() override;
        bool ShouldClose() const override { return m_CloseRequested; }
        void SwapBuffers() override {}

        int GetFramebufferWidth() const override { return m_Width; }
        int GetFramebufferHeight() const override { return m_Height; }
        int GetBytesPerPixel() const override { return 4; }

        void SetTitle(const std::string& title) override { m_Title = title; }
        const std::string& GetTitle() const { return m_Title; }
        void ToggleFullscreen() override {}
        void Destroy() const override {}
        void ProcessInputs() const override {}

        SystemInfos GetSystemInfos() const override;

    private:
        std::string m_Title;
        int m_Width = 1280;
        int m_Height = 720;
        bool m_CloseRequested = false;
        std::deque<OSEvent> m_Pending;
    };

    /// @brief Keyboard, mouse and gamepad state built from the OS events of a window (the same state machine as the SDL
    /// input, without SDL) : IsKeyDown / WasKeyPressed... answer from the events the window dispatched.
    class HeadlessInput : public IInput {
    public:
        explicit HeadlessInput(OSEventSource* window) : m_Window(window) {}
        ~HeadlessInput() override { Shutdown(); }

        void Init() override;
        void Tick() override;
        void Shutdown() override;

        bool IsKeyDown(Input::Key key) const override { return m_Keys[static_cast<size_t>(key)]; }
        bool IsKeyUp(Input::Key key) const override { return !IsKeyDown(key); }
        bool WasKeyPressed(Input::Key key) const override { const size_t i = static_cast<size_t>(key); return !m_PreviousKeys[i] && m_Keys[i]; }
        bool WasKeyReleased(Input::Key key) const override { const size_t i = static_cast<size_t>(key); return m_PreviousKeys[i] && !m_Keys[i]; }

        bool IsMouseDown(Input::MouseButton button) const override { return m_Buttons[static_cast<size_t>(button)]; }
        bool IsMouseUp(Input::MouseButton button) const override { return !IsMouseDown(button); }
        bool WasMousePressed(Input::MouseButton button) const override { const size_t i = static_cast<size_t>(button); return !m_PreviousButtons[i] && m_Buttons[i]; }
        bool WasMouseReleased(Input::MouseButton button) const override { const size_t i = static_cast<size_t>(button); return m_PreviousButtons[i] && !m_Buttons[i]; }

        void SetCursorVisibility(CursorVisibility visibility) const override { m_Captured = visibility == CursorVisibility::Disabled; }
        void GetCursorPos(double* x, double* y) const override { if (x) *x = m_CursorX; if (y) *y = m_CursorY; }
        void SetCursorPos(double x, double y) const override { m_CursorX = x; m_CursorY = y; }

        int GetGamepadCount() const override { return static_cast<int>(m_Pads.size()); }
        std::string GetGamepadName(int pad) const override { return GetPad(pad) ? "Headless gamepad" : ""; }
        bool IsGamepadButtonDown(int pad, Input::GamepadButton button) const override;
        bool WasGamepadButtonPressed(int pad, Input::GamepadButton button) const override;
        bool WasGamepadButtonReleased(int pad, Input::GamepadButton button) const override;
        float GetGamepadAxis(int pad, Input::GamepadAxis axis, float deadZone = 0.1f) const override;

    private:
        static constexpr size_t kKeyCount = static_cast<size_t>(Input::Key::Menu) + 1;
        static constexpr size_t kMouseButtonCount = static_cast<size_t>(Input::MouseButton::Button7) + 1;
        static constexpr size_t kPadButtonCount = static_cast<size_t>(Input::GamepadButton::Count);
        static constexpr size_t kPadAxisCount = static_cast<size_t>(Input::GamepadAxis::Count);

        struct Pad {
            uint32_t id = 0;
            std::array<bool, kPadButtonCount> buttons{};
            std::array<bool, kPadButtonCount> previousButtons{};
            std::array<float, kPadAxisCount> axes{};
        };

        void OnEvent(const OSEvent& event);
        Pad* FindPad(uint32_t id);
        const Pad* GetPad(int index) const { return index >= 0 && index < static_cast<int>(m_Pads.size()) ? &m_Pads[static_cast<size_t>(index)] : nullptr; }

        OSEventSource* m_Window = nullptr;
        OSEventSource::ListenerId m_Listener = 0;
        bool m_Listening = false;

        std::array<bool, kKeyCount> m_Keys{};
        std::array<bool, kKeyCount> m_PreviousKeys{};
        std::array<bool, kMouseButtonCount> m_Buttons{};
        std::array<bool, kMouseButtonCount> m_PreviousButtons{};
        std::vector<Pad> m_Pads;

        mutable double m_CursorX = 0.0;
        mutable double m_CursorY = 0.0;
        mutable bool m_Captured = false;
    };

    /// @brief The platform of a run without a screen (see HeadlessWindow, HeadlessInput). Nothing here starts SDL.
    class HeadlessPlatform : public IPlatform {
    public:
        IWindow* GetWindow() override { return m_Window.get(); }
        IInput* GetInput() override { return m_Input.get(); }

        /// The same window, as what it really is : where a replay posts its events
        HeadlessWindow* GetHeadlessWindow() { return m_Window.get(); }

        void CreateWindow(const std::string& title, const int& width, const int& height,
                          const bool& fullscreen, const int& vsync, const uint32_t& api) override;
        void CreateInput() override;

        void SetClipboardText(const std::string& text) override { m_Clipboard = text; }
        std::string GetClipboardText() override { return m_Clipboard; }

    private:
        std::unique_ptr<HeadlessWindow> m_Window;
        std::unique_ptr<HeadlessInput> m_Input;
        std::string m_Clipboard;
    };
}

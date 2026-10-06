#include "engine/platform/devices/sdl/sdl_input.hpp"

#include <algorithm>

namespace Shard::Engine::Core::Platform {

    SDLInput::SDLInput(SDLWindow* window)
        : m_Window(window)
    {
    }

    SDLInput::~SDLInput()
    {
        Shutdown();
    }

    void SDLInput::Init()
    {
        Clear();

        float x = 0.0f, y = 0.0f;
        SDL_GetMouseState(&x, &y);
        m_CursorX = x;
        m_CursorY = y;

        // The pads that were already plugged in when the engine started
        int padCount = 0;
        if (SDL_JoystickID* pads = SDL_GetGamepads(&padCount))
        {
            for (int i = 0; i < padCount; ++i)
                OpenGamepad(pads[i]);
            SDL_free(pads);
        }

        if (m_Window && !m_Listening)
        {
            m_Listener = m_Window->AddEventListener([this](const OSEvent& event) { OnEvent(event); });
            m_Listening = true;
        }
    }

    void SDLInput::Tick()
    {
        m_PreviousKeys = m_Keys;
        m_PreviousButtons = m_Buttons;
        for (Pad& pad : m_Pads)
            pad.previousButtons = pad.buttons;
    }

    void SDLInput::Shutdown()
    {
        if (m_Window && m_Listening)
        {
            m_Window->RemoveEventListener(m_Listener);
            m_Listening = false;
        }
        for (Pad& pad : m_Pads)
            if (pad.handle)
                SDL_CloseGamepad(pad.handle);
        m_Pads.clear();
        Clear();
    }

    void SDLInput::Clear()
    {
        m_Keys.fill(false);
        m_PreviousKeys.fill(false);
        m_Buttons.fill(false);
        m_PreviousButtons.fill(false);
    }

    void SDLInput::OnEvent(const OSEvent& event)
    {
        switch (event.type)
        {
            case OSEventType::KeyDown:
            case OSEventType::KeyUp:
                m_Keys[static_cast<size_t>(event.key)] = event.type == OSEventType::KeyDown;
                break;
            case OSEventType::MouseButtonDown:
            case OSEventType::MouseButtonUp:
                m_Buttons[static_cast<size_t>(event.button)] = event.type == OSEventType::MouseButtonDown;
                break;
            case OSEventType::MouseMotion:
                if (m_Captured)
                {
                    m_CursorX += event.deltaX;
                    m_CursorY += event.deltaY;
                }
                else
                {
                    m_CursorX = event.mouseX;
                    m_CursorY = event.mouseY;
                }
                break;
            case OSEventType::GamepadConnected:
                OpenGamepad(event.deviceId);
                break;
            case OSEventType::GamepadDisconnected:
                CloseGamepad(event.deviceId);
                break;
            case OSEventType::GamepadButtonDown:
            case OSEventType::GamepadButtonUp:
                if (Pad* pad = FindPad(event.deviceId))
                    pad->buttons[static_cast<size_t>(event.gamepadButton)] = event.type == OSEventType::GamepadButtonDown;
                break;
            case OSEventType::GamepadAxisMotion:
                if (Pad* pad = FindPad(event.deviceId))
                    pad->axes[static_cast<size_t>(event.gamepadAxis)] = event.axisValue;
                break;
            case OSEventType::WindowFocusLost:
                // The release events are not sent to a window that lost the focus.
                m_Keys.fill(false);
                m_Buttons.fill(false);
                break;
            default:
                break;
        }
    }

    bool SDLInput::IsKeyDown(Input::Key key) const
    {
        return m_Keys[static_cast<size_t>(key)];
    }

    bool SDLInput::IsKeyUp(Input::Key key) const
    {
        return !IsKeyDown(key);
    }

    bool SDLInput::WasKeyPressed(Input::Key key) const
    {
        const size_t i = static_cast<size_t>(key);
        return !m_PreviousKeys[i] && m_Keys[i];
    }

    bool SDLInput::WasKeyReleased(Input::Key key) const
    {
        const size_t i = static_cast<size_t>(key);
        return m_PreviousKeys[i] && !m_Keys[i];
    }

    bool SDLInput::IsMouseDown(Input::MouseButton button) const
    {
        return m_Buttons[static_cast<size_t>(button)];
    }

    bool SDLInput::IsMouseUp(Input::MouseButton button) const
    {
        return !IsMouseDown(button);
    }

    bool SDLInput::WasMousePressed(Input::MouseButton button) const
    {
        const size_t i = static_cast<size_t>(button);
        return !m_PreviousButtons[i] && m_Buttons[i];
    }

    bool SDLInput::WasMouseReleased(Input::MouseButton button) const
    {
        const size_t i = static_cast<size_t>(button);
        return m_PreviousButtons[i] && !m_Buttons[i];
    }

    void SDLInput::SetCursorVisibility(CursorVisibility visibility) const
    {
        SDL_Window* window = m_Window->GetSDLWindow();

        if (visibility == CursorVisibility::Visible)
            SDL_ShowCursor();
        else
            SDL_HideCursor();

        // Disabled : the cursor is hidden and captured, the motion is reported as raw relative deltas.
        const bool capture = visibility == CursorVisibility::Disabled;
        SDL_SetWindowRelativeMouseMode(window, capture);
        m_Captured = capture;
    }

    void SDLInput::GetCursorPos(double* x, double* y) const
    {
        if (x) *x = m_CursorX;
        if (y) *y = m_CursorY;
    }

    void SDLInput::SetCursorPos(double x, double y) const
    {
        m_CursorX = x;
        m_CursorY = y;
        if (!m_Captured)
            SDL_WarpMouseInWindow(m_Window->GetSDLWindow(), static_cast<float>(x), static_cast<float>(y));
    }

    void SDLInput::OpenGamepad(SDL_JoystickID id)
    {
        if (FindPad(id))
            return;     // SDL also reports the pads that were plugged in before the start
        SDL_Gamepad* handle = SDL_OpenGamepad(id);
        if (!handle)
            return;
        Pad pad;
        pad.id = id;
        pad.handle = handle;
        m_Pads.push_back(pad);
    }

    void SDLInput::CloseGamepad(SDL_JoystickID id)
    {
        for (auto it = m_Pads.begin(); it != m_Pads.end(); ++it)
        {
            if (it->id != id)
                continue;
            if (it->handle)
                SDL_CloseGamepad(it->handle);
            m_Pads.erase(it);
            return;
        }
    }

    SDLInput::Pad* SDLInput::FindPad(uint32_t id)
    {
        for (Pad& pad : m_Pads)
            if (pad.id == id)
                return &pad;
        return nullptr;
    }

    const SDLInput::Pad* SDLInput::GetPad(int index) const
    {
        return index >= 0 && index < static_cast<int>(m_Pads.size()) ? &m_Pads[static_cast<size_t>(index)] : nullptr;
    }

    std::string SDLInput::GetGamepadName(int pad) const
    {
        const Pad* p = GetPad(pad);
        const char* name = p && p->handle ? SDL_GetGamepadName(p->handle) : nullptr;
        return name ? name : "";
    }

    bool SDLInput::IsGamepadButtonDown(int pad, Input::GamepadButton button) const
    {
        const Pad* p = GetPad(pad);
        return p && p->buttons[static_cast<size_t>(button)];
    }

    bool SDLInput::WasGamepadButtonPressed(int pad, Input::GamepadButton button) const
    {
        const Pad* p = GetPad(pad);
        return p && !p->previousButtons[static_cast<size_t>(button)] && p->buttons[static_cast<size_t>(button)];
    }

    bool SDLInput::WasGamepadButtonReleased(int pad, Input::GamepadButton button) const
    {
        const Pad* p = GetPad(pad);
        return p && p->previousButtons[static_cast<size_t>(button)] && !p->buttons[static_cast<size_t>(button)];
    }

    float SDLInput::GetGamepadAxis(int pad, Input::GamepadAxis axis, float deadZone) const
    {
        const Pad* p = GetPad(pad);
        if (!p)
            return 0.0f;
        const float value = p->axes[static_cast<size_t>(axis)];
        return (value < deadZone && value > -deadZone) ? 0.0f : value;
    }

    bool SDLInput::SetGamepadRumble(int pad, float lowFrequency, float highFrequency, int durationMs)
    {
        const Pad* p = GetPad(pad);
        if (!p || !p->handle)
            return false;
        const auto motor = [](float strength) { return static_cast<Uint16>(std::clamp(strength, 0.0f, 1.0f) * 65535.0f); };
        return SDL_RumbleGamepad(p->handle, motor(lowFrequency), motor(highFrequency), static_cast<Uint32>(std::max(durationMs, 0)));
    }
}

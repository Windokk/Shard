#include "engine/platform/devices/sdl/sdl_keymap.hpp"

#include <array>

namespace Shard::Engine::Core::Platform {

    namespace {
        using Input::Key;

        constexpr size_t kKeyCount = static_cast<size_t>(Key::Menu) + 1;

        // Same order as Input::Key.
        constexpr std::array<SDL_Scancode, kKeyCount> kKeyToScancode = {
            SDL_SCANCODE_SPACE, SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_COMMA, SDL_SCANCODE_MINUS, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH,
            SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
            SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9,
            SDL_SCANCODE_SEMICOLON,
            SDL_SCANCODE_EQUALS,
            SDL_SCANCODE_A, SDL_SCANCODE_B, SDL_SCANCODE_C, SDL_SCANCODE_D, SDL_SCANCODE_E, SDL_SCANCODE_F, SDL_SCANCODE_G,
            SDL_SCANCODE_H, SDL_SCANCODE_I, SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_M,
            SDL_SCANCODE_N, SDL_SCANCODE_O, SDL_SCANCODE_P, SDL_SCANCODE_Q, SDL_SCANCODE_R, SDL_SCANCODE_S, SDL_SCANCODE_T,
            SDL_SCANCODE_U, SDL_SCANCODE_V, SDL_SCANCODE_W, SDL_SCANCODE_X, SDL_SCANCODE_Y, SDL_SCANCODE_Z,
            SDL_SCANCODE_LEFTBRACKET,
            SDL_SCANCODE_BACKSLASH,
            SDL_SCANCODE_RIGHTBRACKET,
            SDL_SCANCODE_GRAVE,
            SDL_SCANCODE_NONUSBACKSLASH,    // World1
            SDL_SCANCODE_NONUSHASH,         // World2

            SDL_SCANCODE_ESCAPE,
            SDL_SCANCODE_RETURN,
            SDL_SCANCODE_TAB,
            SDL_SCANCODE_BACKSPACE,
            SDL_SCANCODE_INSERT,
            SDL_SCANCODE_DELETE,
            SDL_SCANCODE_RIGHT,
            SDL_SCANCODE_LEFT,
            SDL_SCANCODE_DOWN,
            SDL_SCANCODE_UP,
            SDL_SCANCODE_PAGEUP,
            SDL_SCANCODE_PAGEDOWN,
            SDL_SCANCODE_HOME,
            SDL_SCANCODE_END,
            SDL_SCANCODE_CAPSLOCK,
            SDL_SCANCODE_SCROLLLOCK,
            SDL_SCANCODE_NUMLOCKCLEAR,
            SDL_SCANCODE_PRINTSCREEN,
            SDL_SCANCODE_PAUSE,
            SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4, SDL_SCANCODE_F5, SDL_SCANCODE_F6,
            SDL_SCANCODE_F7, SDL_SCANCODE_F8, SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_F11, SDL_SCANCODE_F12,
            SDL_SCANCODE_F13, SDL_SCANCODE_F14, SDL_SCANCODE_F15, SDL_SCANCODE_F16, SDL_SCANCODE_F17,
            SDL_SCANCODE_F18, SDL_SCANCODE_F19, SDL_SCANCODE_F20, SDL_SCANCODE_F21, SDL_SCANCODE_F22, SDL_SCANCODE_F23, SDL_SCANCODE_F24,
            SDL_SCANCODE_UNKNOWN,           // F25

            SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_4,
            SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9,
            SDL_SCANCODE_KP_PERIOD,
            SDL_SCANCODE_KP_DIVIDE,
            SDL_SCANCODE_KP_MULTIPLY,
            SDL_SCANCODE_KP_MINUS,
            SDL_SCANCODE_KP_PLUS,
            SDL_SCANCODE_KP_ENTER,
            SDL_SCANCODE_KP_EQUALS,

            SDL_SCANCODE_LSHIFT,
            SDL_SCANCODE_LCTRL,
            SDL_SCANCODE_LALT,
            SDL_SCANCODE_LGUI,
            SDL_SCANCODE_RSHIFT,
            SDL_SCANCODE_RCTRL,
            SDL_SCANCODE_RALT,
            SDL_SCANCODE_RGUI,
            SDL_SCANCODE_APPLICATION,       // Menu
        };

        struct ReverseMap {
            std::array<int, SDL_SCANCODE_COUNT> keys;
            ReverseMap() {
                keys.fill(-1);
                for (size_t i = 0; i < kKeyCount; ++i)
                    if (kKeyToScancode[i] != SDL_SCANCODE_UNKNOWN)
                        keys[kKeyToScancode[i]] = static_cast<int>(i);
            }
        };
    }

    bool ScancodeToKey(SDL_Scancode scancode, Input::Key& out)
    {
        static const ReverseMap reverse;
        if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT)
            return false;
        const int key = reverse.keys[scancode];
        if (key < 0)
            return false;
        out = static_cast<Input::Key>(key);
        return true;
    }

    SDL_Scancode KeyToScancode(Input::Key key)
    {
        const size_t index = static_cast<size_t>(key);
        return index < kKeyCount ? kKeyToScancode[index] : SDL_SCANCODE_UNKNOWN;
    }

    bool SDLButtonToMouseButton(uint8_t sdlButton, Input::MouseButton& out)
    {
        switch (sdlButton)
        {
            case SDL_BUTTON_LEFT:   out = Input::MouseButton::Left; return true;
            case SDL_BUTTON_RIGHT:  out = Input::MouseButton::Right; return true;
            case SDL_BUTTON_MIDDLE: out = Input::MouseButton::Middle; return true;
            case SDL_BUTTON_X1:     out = Input::MouseButton::Button3; return true;
            case SDL_BUTTON_X2:     out = Input::MouseButton::Button4; return true;
            default: return false;
        }
    }

    // The engine enums follow SDL's order : the conversion is a range check
    static_assert(static_cast<int>(Input::GamepadButton::South) == SDL_GAMEPAD_BUTTON_SOUTH);
    static_assert(static_cast<int>(Input::GamepadButton::DpadRight) == SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    static_assert(static_cast<int>(Input::GamepadAxis::LeftX) == SDL_GAMEPAD_AXIS_LEFTX);
    static_assert(static_cast<int>(Input::GamepadAxis::RightTrigger) == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

    bool SDLGamepadButtonToButton(uint8_t sdlButton, Input::GamepadButton& out)
    {
        if (sdlButton >= static_cast<int>(Input::GamepadButton::Count))
            return false;
        out = static_cast<Input::GamepadButton>(sdlButton);
        return true;
    }

    bool SDLGamepadAxisToAxis(uint8_t sdlAxis, Input::GamepadAxis& out)
    {
        if (sdlAxis >= static_cast<int>(Input::GamepadAxis::Count))
            return false;
        out = static_cast<Input::GamepadAxis>(sdlAxis);
        return true;
    }
}

#pragma once

#include "engine/platform/devices/gamepad.hpp"
#include "engine/platform/devices/keys.hpp"

#include <SDL3/SDL.h>

namespace Shard::Engine::Core::Platform {

    /// @brief Physical key (SDL scancode, independent of the keyboard layout) -> engine key. False when the engine has no such key.
    bool ScancodeToKey(SDL_Scancode scancode, Input::Key& out);

    /// @brief Engine key -> SDL scancode (SDL_SCANCODE_UNKNOWN when there is none).
    SDL_Scancode KeyToScancode(Input::Key key);

    /// @brief SDL gamepad button / axis -> engine ones. False for the ones the engine doesn't expose (touchpad, paddles...).
    bool SDLGamepadButtonToButton(uint8_t sdlButton, Input::GamepadButton& out);
    bool SDLGamepadAxisToAxis(uint8_t sdlAxis, Input::GamepadAxis& out);

    /// @brief SDL mouse button (SDL_BUTTON_LEFT...) -> engine button.
    bool SDLButtonToMouseButton(uint8_t sdlButton, Input::MouseButton& out);
}

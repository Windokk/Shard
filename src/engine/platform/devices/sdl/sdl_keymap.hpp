#pragma once

#include "engine/platform/devices/gamepad.hpp"
#include "engine/platform/devices/keys.hpp"

#include <SDL3/SDL.h>

namespace Shard::Engine::Core::Platform {

    bool ScancodeToKey(SDL_Scancode scancode, Input::Key& out);

    SDL_Scancode KeyToScancode(Input::Key key);

    bool SDLGamepadButtonToButton(uint8_t sdlButton, Input::GamepadButton& out);
    bool SDLGamepadAxisToAxis(uint8_t sdlAxis, Input::GamepadAxis& out);

    bool SDLButtonToMouseButton(uint8_t sdlButton, Input::MouseButton& out);
}

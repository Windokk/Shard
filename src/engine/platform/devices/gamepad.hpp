#pragma once

namespace Shard::Engine::Input {

    // Same layout as SDL's gamepad buttons : the face buttons are named by position (South = A on Xbox, Cross on PlayStation)
    enum class GamepadButton {
        South, East, West, North,
        Back, Guide, Start,
        LeftStick, RightStick,
        LeftShoulder, RightShoulder,
        DpadUp, DpadDown, DpadLeft, DpadRight,
        Count
    };

    enum class GamepadAxis {
        LeftX, LeftY,           // -1 (left / up) .. 1 (right / down)
        RightX, RightY,
        LeftTrigger,            // 0 .. 1
        RightTrigger,
        Count
    };
}

#pragma once

namespace Shard::Engine::Input {

    enum class GamepadButton {
        South, East, West, North,
        Back, Guide, Start,
        LeftStick, RightStick,
        LeftShoulder, RightShoulder,
        DpadUp, DpadDown, DpadLeft, DpadRight,
        Count
    };

    enum class GamepadAxis {
        LeftX, LeftY,
        RightX, RightY,
        LeftTrigger,
        RightTrigger,
        Count
    };
}

// Shard C++ SDK : keyboard, mouse and gamepads (shard_input.h).
//
//   if (Shard::Input::IsKeyDown(Shard::Key::W)) owner.Translate(owner.Forward() * speed * dt);
//   const float turn = Shard::Input::Axis(Shard::Key::A, Shard::Key::D);                 // -1, 0 or 1
//   if (Shard::Input::WasPressed(Shard::Key::Space)) Jump();
//
// What the engine's input manager sees this frame, which is also what a replay records. Call it from the callbacks of a script.
#pragma once

#include <shard/shard_input.h>
#include <shard/cpp/detail.hpp>
#include <shard/cpp/math.hpp>

namespace Shard {

    enum class Key : int32_t {
        Space = SHARD_KEY_SPACE, Apostrophe = SHARD_KEY_APOSTROPHE, Comma = SHARD_KEY_COMMA, Minus = SHARD_KEY_MINUS, Period = SHARD_KEY_PERIOD,
        Slash = SHARD_KEY_SLASH,
        K0 = SHARD_KEY_0, K1, K2, K3, K4, K5, K6, K7, K8, K9,
        Semicolon = SHARD_KEY_SEMICOLON, Equal = SHARD_KEY_EQUAL,
        A = SHARD_KEY_A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        LeftBracket = SHARD_KEY_LEFT_BRACKET, Backslash = SHARD_KEY_BACKSLASH, RightBracket = SHARD_KEY_RIGHT_BRACKET, GraveAccent = SHARD_KEY_GRAVE_ACCENT,
        Escape = SHARD_KEY_ESCAPE, Enter = SHARD_KEY_ENTER, Tab = SHARD_KEY_TAB, Backspace = SHARD_KEY_BACKSPACE, Insert = SHARD_KEY_INSERT,
        Delete = SHARD_KEY_DELETE, Right = SHARD_KEY_RIGHT, Left = SHARD_KEY_LEFT, Down = SHARD_KEY_DOWN, Up = SHARD_KEY_UP,
        PageUp = SHARD_KEY_PAGE_UP, PageDown = SHARD_KEY_PAGE_DOWN, Home = SHARD_KEY_HOME, End = SHARD_KEY_END,
        CapsLock = SHARD_KEY_CAPS_LOCK, ScrollLock = SHARD_KEY_SCROLL_LOCK, NumLock = SHARD_KEY_NUM_LOCK, PrintScreen = SHARD_KEY_PRINT_SCREEN,
        Pause = SHARD_KEY_PAUSE,
        F1 = SHARD_KEY_F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        Keypad0 = SHARD_KEY_KP_0, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
        KeypadDecimal = SHARD_KEY_KP_DECIMAL, KeypadDivide = SHARD_KEY_KP_DIVIDE, KeypadMultiply = SHARD_KEY_KP_MULTIPLY,
        KeypadSubtract = SHARD_KEY_KP_SUBTRACT, KeypadAdd = SHARD_KEY_KP_ADD, KeypadEnter = SHARD_KEY_KP_ENTER, KeypadEqual = SHARD_KEY_KP_EQUAL,
        LeftShift = SHARD_KEY_LEFT_SHIFT, LeftControl = SHARD_KEY_LEFT_CONTROL, LeftAlt = SHARD_KEY_LEFT_ALT, LeftSuper = SHARD_KEY_LEFT_SUPER,
        RightShift = SHARD_KEY_RIGHT_SHIFT, RightControl = SHARD_KEY_RIGHT_CONTROL, RightAlt = SHARD_KEY_RIGHT_ALT, RightSuper = SHARD_KEY_RIGHT_SUPER,
        Menu = SHARD_KEY_MENU,
    };

    enum class MouseButton : int32_t { Left = SHARD_MOUSE_LEFT, Right = SHARD_MOUSE_RIGHT, Middle = SHARD_MOUSE_MIDDLE };

    enum class CursorMode : int32_t { Visible = SHARD_CURSOR_VISIBLE, Hidden = SHARD_CURSOR_HIDDEN, Disabled = SHARD_CURSOR_DISABLED };

    /// The face buttons are named by position: South is A on an Xbox pad, Cross on a PlayStation one
    enum class PadButton : int32_t {
        South = SHARD_PAD_SOUTH, East = SHARD_PAD_EAST, West = SHARD_PAD_WEST, North = SHARD_PAD_NORTH, Back = SHARD_PAD_BACK, Guide = SHARD_PAD_GUIDE,
        Start = SHARD_PAD_START, LeftStick = SHARD_PAD_LEFT_STICK, RightStick = SHARD_PAD_RIGHT_STICK, LeftShoulder = SHARD_PAD_LEFT_SHOULDER,
        RightShoulder = SHARD_PAD_RIGHT_SHOULDER, DpadUp = SHARD_PAD_DPAD_UP, DpadDown = SHARD_PAD_DPAD_DOWN, DpadLeft = SHARD_PAD_DPAD_LEFT,
        DpadRight = SHARD_PAD_DPAD_RIGHT,
    };

    enum class PadAxis : int32_t {
        LeftX = SHARD_AXIS_LEFT_X, LeftY = SHARD_AXIS_LEFT_Y, RightX = SHARD_AXIS_RIGHT_X, RightY = SHARD_AXIS_RIGHT_Y,
        LeftTrigger = SHARD_AXIS_LEFT_TRIGGER, RightTrigger = SHARD_AXIS_RIGHT_TRIGGER,
    };

    class Input {
    public:
        // ---- Keyboard : IsKeyDown while held, WasPressed / WasReleased on the frame it changes ----
        static bool IsKeyDown(Key key) { return Api().key_down(int32_t(key)) == SHARD_TRUE; }
        static bool WasPressed(Key key) { return Api().key_pressed(int32_t(key)) == SHARD_TRUE; }
        static bool WasReleased(Key key) { return Api().key_released(int32_t(key)) == SHARD_TRUE; }
        /// -1 while `negative` is held, 1 while `positive` is, 0 if neither or both
        static float Axis(Key negative, Key positive) { return (IsKeyDown(positive) ? 1.f : 0.f) - (IsKeyDown(negative) ? 1.f : 0.f); }

        // ---- Mouse ----
        static bool IsMouseDown(MouseButton button) { return Api().mouse_down(int32_t(button)) == SHARD_TRUE; }
        static bool WasMousePressed(MouseButton button) { return Api().mouse_pressed(int32_t(button)) == SHARD_TRUE; }
        static bool WasMouseReleased(MouseButton button) { return Api().mouse_released(int32_t(button)) == SHARD_TRUE; }
        /// The cursor in pixels of the window
        static Vec2 CursorPosition()
        {
            double x = 0.0, y = 0.0;
            Api().get_cursor(&x, &y);
            return { float(x), float(y) };
        }
        static void SetCursorPosition(double x, double y) { Api().set_cursor(x, y); }
        static void SetCursorMode(CursorMode mode) { Api().set_cursor_mode(int32_t(mode)); }

        // ---- Gamepads, from 0 in the order they were plugged in ----
        static int GamepadCount() { return Api().gamepad_count(); }
        static bool IsPadDown(int pad, PadButton button) { return Api().gamepad_button_down(pad, int32_t(button)) == SHARD_TRUE; }
        static bool WasPadPressed(int pad, PadButton button) { return Api().gamepad_button_pressed(pad, int32_t(button)) == SHARD_TRUE; }
        static bool WasPadReleased(int pad, PadButton button) { return Api().gamepad_button_released(pad, int32_t(button)) == SHARD_TRUE; }
        /// Sticks -1 to 1, triggers 0 to 1; what lies within `deadZone` of the centre reads as 0
        static float PadAxisValue(int pad, PadAxis axis, float deadZone = 0.1f) { return Api().gamepad_axis(pad, int32_t(axis), deadZone); }
        /// False if the pad cannot rumble
        static bool Rumble(int pad, float low, float high, int durationMs) { return Api().gamepad_rumble(pad, low, high, durationMs) == SHARD_TRUE; }

    private:
        static const ShardInputApi& Api() { return detail::Require<ShardInputApi>(SHARD_IFACE_INPUT); }
    };
}

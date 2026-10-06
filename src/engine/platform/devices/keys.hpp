#pragma once

namespace Shard::Engine::Input {

    // Printable keys
    enum class Key {
        Space,
        Apostrophe,
        Comma,
        Minus,
        Period,
        Slash,
        K0, K1, K2, K3, K4, K5, K6, K7, K8, K9,
        Semicolon,
        Equal,
        A, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        LeftBracket,
        Backslash,
        RightBracket,
        GraveAccent,
        World1,
        World2,

        // Function keys
        Escape,
        Enter,
        Tab,
        Backspace,
        Insert,
        Delete,
        Right,
        Left,
        Down,
        Up,
        PageUp,
        PageDown,
        Home,
        End,
        CapsLock,
        ScrollLock,
        NumLock,
        PrintScreen,
        Pause,
        F1, F2, F3, F4, F5, F6, F7, F8, F9,
        F10, F11, F12, F13, F14, F15, F16, F17,
        F18, F19, F20, F21, F22, F23, F24, F25,

        // Keypad
        Keypad0, Keypad1, Keypad2, Keypad3, Keypad4,
        Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
        KeypadDecimal,
        KeypadDivide,
        KeypadMultiply,
        KeypadSubtract,
        KeypadAdd,
        KeypadEnter,
        KeypadEqual,

        // Modifier keys
        LeftShift,
        LeftControl,
        LeftAlt,
        LeftSuper,
        RightShift,
        RightControl,
        RightAlt,
        RightSuper,
        Menu,
    };

    // Modifier flags
    enum class Modifier {
        Shift,
        Control,
        Alt,
        Super,
        CapsLock,
        NumLock
    };

    // Mouse buttons
    enum class MouseButton {
        Button0,
        Button1,
        Button2,
        Button3,
        Button4,
        Button5,
        Button6,
        Button7,

        Left   = Button0,
        Right  = Button1,
        Middle = Button2
    };
}

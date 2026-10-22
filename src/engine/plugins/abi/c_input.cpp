#include <unordered_map>

#include <shard/shard_input.h>

#include "engine/plugins/abi/c_internal.hpp"
#include "engine/platform/devices/iinput.hpp"
#include "engine/world/engine.hpp"

// shard.input : the engine's input manager (what a replay records) through key numbers of the ABI's own.

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        using Input::Key;

        const std::unordered_map<int32_t, Key>& KeyTable()
        {
            static const std::unordered_map<int32_t, Key> table = [] {
                std::unordered_map<int32_t, Key> t;
                t[SHARD_KEY_SPACE] = Key::Space;
                t[SHARD_KEY_APOSTROPHE] = Key::Apostrophe;
                t[SHARD_KEY_COMMA] = Key::Comma;
                t[SHARD_KEY_MINUS] = Key::Minus;
                t[SHARD_KEY_PERIOD] = Key::Period;
                t[SHARD_KEY_SLASH] = Key::Slash;
                for (int i = 0; i < 10; ++i) t[SHARD_KEY_0 + i] = Key(int(Key::K0) + i);
                t[SHARD_KEY_SEMICOLON] = Key::Semicolon;
                t[SHARD_KEY_EQUAL] = Key::Equal;
                for (int i = 0; i < 26; ++i) t[SHARD_KEY_A + i] = Key(int(Key::A) + i);
                t[SHARD_KEY_LEFT_BRACKET] = Key::LeftBracket;
                t[SHARD_KEY_BACKSLASH] = Key::Backslash;
                t[SHARD_KEY_RIGHT_BRACKET] = Key::RightBracket;
                t[SHARD_KEY_GRAVE_ACCENT] = Key::GraveAccent;
                t[SHARD_KEY_ESCAPE] = Key::Escape;
                t[SHARD_KEY_ENTER] = Key::Enter;
                t[SHARD_KEY_TAB] = Key::Tab;
                t[SHARD_KEY_BACKSPACE] = Key::Backspace;
                t[SHARD_KEY_INSERT] = Key::Insert;
                t[SHARD_KEY_DELETE] = Key::Delete;
                t[SHARD_KEY_RIGHT] = Key::Right;
                t[SHARD_KEY_LEFT] = Key::Left;
                t[SHARD_KEY_DOWN] = Key::Down;
                t[SHARD_KEY_UP] = Key::Up;
                t[SHARD_KEY_PAGE_UP] = Key::PageUp;
                t[SHARD_KEY_PAGE_DOWN] = Key::PageDown;
                t[SHARD_KEY_HOME] = Key::Home;
                t[SHARD_KEY_END] = Key::End;
                t[SHARD_KEY_CAPS_LOCK] = Key::CapsLock;
                t[SHARD_KEY_SCROLL_LOCK] = Key::ScrollLock;
                t[SHARD_KEY_NUM_LOCK] = Key::NumLock;
                t[SHARD_KEY_PRINT_SCREEN] = Key::PrintScreen;
                t[SHARD_KEY_PAUSE] = Key::Pause;
                for (int i = 0; i < 25; ++i) t[SHARD_KEY_F1 + i] = Key(int(Key::F1) + i);
                for (int i = 0; i < 10; ++i) t[SHARD_KEY_KP_0 + i] = Key(int(Key::Keypad0) + i);
                t[SHARD_KEY_KP_DECIMAL] = Key::KeypadDecimal;
                t[SHARD_KEY_KP_DIVIDE] = Key::KeypadDivide;
                t[SHARD_KEY_KP_MULTIPLY] = Key::KeypadMultiply;
                t[SHARD_KEY_KP_SUBTRACT] = Key::KeypadSubtract;
                t[SHARD_KEY_KP_ADD] = Key::KeypadAdd;
                t[SHARD_KEY_KP_ENTER] = Key::KeypadEnter;
                t[SHARD_KEY_KP_EQUAL] = Key::KeypadEqual;
                t[SHARD_KEY_LEFT_SHIFT] = Key::LeftShift;
                t[SHARD_KEY_LEFT_CONTROL] = Key::LeftControl;
                t[SHARD_KEY_LEFT_ALT] = Key::LeftAlt;
                t[SHARD_KEY_LEFT_SUPER] = Key::LeftSuper;
                t[SHARD_KEY_RIGHT_SHIFT] = Key::RightShift;
                t[SHARD_KEY_RIGHT_CONTROL] = Key::RightControl;
                t[SHARD_KEY_RIGHT_ALT] = Key::RightAlt;
                t[SHARD_KEY_RIGHT_SUPER] = Key::RightSuper;
                t[SHARD_KEY_MENU] = Key::Menu;
                return t;
            }();
            return table;
        }

        Core::Platform::IInput* Manager()
        {
            if (!OnScriptThread()) return nullptr;
            // GetEngine() ends the process when no engine is set (an offline tool, a test) : the pointer is read instead
            Core::IEngineContext* engine = Core::gSharedEnginePtr;
            return engine ? engine->GetInputManager() : nullptr;
        }

        bool MapKey(int32_t key, Key& out)
        {
            const auto& table = KeyTable();
            auto it = table.find(key);
            if (it == table.end()) return false;
            out = it->second;
            return true;
        }

        bool MapButton(int32_t button, Input::MouseButton& out)
        {
            if (button < 0 || button > 7) return false;
            out = Input::MouseButton(button);
            return true;
        }

        bool MapPadButton(int32_t button, Input::GamepadButton& out)
        {
            if (button < 0 || button >= int(Input::GamepadButton::Count)) return false;
            out = Input::GamepadButton(button);
            return true;
        }

        int32_t SHARD_CALL KeyDown(int32_t key)
        {
            try { Key k; auto* in = Manager(); return in && MapKey(key, k) && in->IsKeyDown(k) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
        int32_t SHARD_CALL KeyPressed(int32_t key)
        {
            try { Key k; auto* in = Manager(); return in && MapKey(key, k) && in->WasKeyPressed(k) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
        int32_t SHARD_CALL KeyReleased(int32_t key)
        {
            try { Key k; auto* in = Manager(); return in && MapKey(key, k) && in->WasKeyReleased(k) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }

        int32_t SHARD_CALL MouseDown(int32_t button)
        {
            try { Input::MouseButton b; auto* in = Manager(); return in && MapButton(button, b) && in->IsMouseDown(b) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
        int32_t SHARD_CALL MousePressed(int32_t button)
        {
            try { Input::MouseButton b; auto* in = Manager(); return in && MapButton(button, b) && in->WasMousePressed(b) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
        int32_t SHARD_CALL MouseReleased(int32_t button)
        {
            try { Input::MouseButton b; auto* in = Manager(); return in && MapButton(button, b) && in->WasMouseReleased(b) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }

        ShardResult SHARD_CALL GetCursor(double* x, double* y)
        {
            try {
                if (!x || !y) return SHARD_E_INVALID_ARG;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                auto* in = Manager();
                if (!in) return SHARD_E_UNSUPPORTED;
                in->GetCursorPos(x, y);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetCursor(double x, double y)
        {
            try {
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                auto* in = Manager();
                if (!in) return SHARD_E_UNSUPPORTED;
                in->SetCursorPos(x, y);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetCursorMode(int32_t mode)
        {
            try {
                if (mode < SHARD_CURSOR_VISIBLE || mode > SHARD_CURSOR_DISABLED) return SHARD_E_INVALID_ARG;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                auto* in = Manager();
                if (!in) return SHARD_E_UNSUPPORTED;
                in->SetCursorVisibility(Core::Platform::CursorVisibility(mode));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        int32_t SHARD_CALL GamepadCount()
        {
            try { auto* in = Manager(); return in ? in->GetGamepadCount() : 0; } catch (...) { return 0; }
        }
        int32_t SHARD_CALL GamepadButtonDown(int32_t pad, int32_t button)
        {
            try { Input::GamepadButton b; auto* in = Manager(); return in && MapPadButton(button, b) && in->IsGamepadButtonDown(pad, b) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
        int32_t SHARD_CALL GamepadButtonPressed(int32_t pad, int32_t button)
        {
            try { Input::GamepadButton b; auto* in = Manager(); return in && MapPadButton(button, b) && in->WasGamepadButtonPressed(pad, b) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
        int32_t SHARD_CALL GamepadButtonReleased(int32_t pad, int32_t button)
        {
            try { Input::GamepadButton b; auto* in = Manager(); return in && MapPadButton(button, b) && in->WasGamepadButtonReleased(pad, b) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
        float SHARD_CALL GamepadAxis(int32_t pad, int32_t axis, float deadZone)
        {
            try {
                auto* in = Manager();
                if (!in || axis < 0 || axis >= int(Input::GamepadAxis::Count)) return 0.0f;
                return in->GetGamepadAxis(pad, Input::GamepadAxis(axis), deadZone);
            } catch (...) { return 0.0f; }
        }
        int32_t SHARD_CALL GamepadRumble(int32_t pad, float low, float high, int32_t durationMs)
        {
            try { auto* in = Manager(); return in && in->SetGamepadRumble(pad, low, high, durationMs) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
    }

    const void* InputApi()
    {
        static const ShardInputApi api = {
            sizeof(ShardInputApi),
            &KeyDown, &KeyPressed, &KeyReleased,
            &MouseDown, &MousePressed, &MouseReleased, &GetCursor, &SetCursor, &SetCursorMode,
            &GamepadCount, &GamepadButtonDown, &GamepadButtonPressed, &GamepadButtonReleased, &GamepadAxis, &GamepadRumble };
        return &api;
    }
}

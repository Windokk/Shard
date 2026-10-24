#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <shard/shard_input.h>

#include "engine/plugins/abi/c_internal.hpp"
#include "engine/platform/clipboard/clipboard.hpp"
#include "engine/platform/devices/iinput.hpp"
#include "engine/platform/windowing/iwindow.hpp"
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

        // What the UIs of the plugins take (shard_input.h, capture) : per plugin, with the frame it was last said, so that a plugin that stops
        // saying it lets go
        struct Capture { uint32_t mask = 0; uint64_t frame = 0; };
        std::mutex gInputMutex;
        std::unordered_map<PluginOwner, Capture> gCaptures;
        uint64_t gInputFrame = 0;
        std::atomic<bool> gInputUsed{ false };       // a plugin asked for the events or took the input : until then the window is not looked at (a test has none)

        uint32_t Captured()
        {
            std::lock_guard<std::mutex> lock(gInputMutex);
            uint32_t mask = 0;
            for (const auto& [owner, capture] : gCaptures) if (capture.frame + 1 >= gInputFrame) mask |= capture.mask;
            return mask;
        }

        // The events of the frame, from the window system, in order
        std::vector<ShardInputEvent> gEvents;
        uint32_t gMods = 0;
        bool gListening = false;

        uint32_t ModOf(int32_t key)
        {
            switch (key) {
                case SHARD_KEY_LEFT_SHIFT: case SHARD_KEY_RIGHT_SHIFT: return SHARD_MOD_SHIFT;
                case SHARD_KEY_LEFT_CONTROL: case SHARD_KEY_RIGHT_CONTROL: return SHARD_MOD_CONTROL;
                case SHARD_KEY_LEFT_ALT: case SHARD_KEY_RIGHT_ALT: return SHARD_MOD_ALT;
                case SHARD_KEY_LEFT_SUPER: case SHARD_KEY_RIGHT_SUPER: return SHARD_MOD_SUPER;
                default: return 0;
            }
        }

        int32_t ShardKeyOf(Key key)
        {
            static const std::unordered_map<int, int32_t> reverse = [] {
                std::unordered_map<int, int32_t> r;
                for (const auto& [shardKey, engineKey] : KeyTable()) r[int(engineKey)] = shardKey;
                return r;
            }();
            auto it = reverse.find(int(key));
            return it == reverse.end() ? -1 : it->second;
        }

        // The Unicode characters of a UTF-8 text
        void ForEachCodepoint(const std::string& text, const std::function<void(uint32_t)>& fn)
        {
            for (size_t i = 0; i < text.size();) {
                const unsigned char c = (unsigned char)text[i];
                uint32_t cp = 0;
                size_t extra = 0;
                if (c < 0x80) { cp = c; }
                else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
                else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
                else if ((c >> 3) == 0x1E) { cp = c & 0x07; extra = 3; }
                else { ++i; continue; }
                if (i + extra >= text.size() && extra) break;
                for (size_t k = 1; k <= extra; ++k) cp = (cp << 6) | ((unsigned char)text[i + k] & 0x3F);
                i += extra + 1;
                fn(cp);
            }
        }

        void OnOsEvent(const Core::Platform::OSEvent& e)
        {
            using Core::Platform::OSEventType;
            ShardInputEvent out{};
            out.structSize = sizeof(ShardInputEvent);
            std::lock_guard<std::mutex> lock(gInputMutex);
            out.mods = gMods;
            switch (e.type) {
                case OSEventType::KeyDown:
                case OSEventType::KeyUp: {
                    const int32_t key = ShardKeyOf(e.key);
                    if (key < 0) return;
                    const bool down = e.type == OSEventType::KeyDown;
                    if (const uint32_t mod = ModOf(key)) { if (down) gMods |= mod; else gMods &= ~mod; out.mods = gMods; }
                    out.kind = down ? SHARD_EVENT_KEY_DOWN : SHARD_EVENT_KEY_UP;
                    out.key = key;
                    out.flags = e.repeat ? SHARD_EVENT_FLAG_REPEAT : 0;
                    gEvents.push_back(out);
                    return;
                }
                case OSEventType::TextInput:
                    ForEachCodepoint(e.text, [&](uint32_t cp) {
                        ShardInputEvent t = out;
                        t.kind = SHARD_EVENT_TEXT;
                        t.codepoint = cp;
                        gEvents.push_back(t);
                    });
                    return;
                case OSEventType::MouseMotion:
                    out.kind = SHARD_EVENT_MOUSE_MOVE; out.x = e.mouseX; out.y = e.mouseY; out.dx = e.deltaX; out.dy = e.deltaY;
                    break;
                case OSEventType::MouseButtonDown:
                case OSEventType::MouseButtonUp:
                    out.kind = e.type == OSEventType::MouseButtonDown ? SHARD_EVENT_MOUSE_DOWN : SHARD_EVENT_MOUSE_UP;
                    out.key = int32_t(e.button); out.x = e.mouseX; out.y = e.mouseY;
                    break;
                case OSEventType::MouseWheel:
                    out.kind = SHARD_EVENT_WHEEL; out.dx = e.deltaX; out.dy = e.deltaY; out.x = e.mouseX; out.y = e.mouseY;
                    break;
                case OSEventType::WindowFocusGained: out.kind = SHARD_EVENT_FOCUS_GAINED; break;
                case OSEventType::WindowFocusLost:
                    gMods = 0;
                    out.kind = SHARD_EVENT_FOCUS_LOST; out.mods = 0;
                    break;
                case OSEventType::WindowResized: out.kind = SHARD_EVENT_RESIZED; out.x = float(e.width); out.y = float(e.height); break;
                default: return;
            }
            gEvents.push_back(out);
        }

        Core::Platform::IWindow* Window()
        {
            Core::IEngineContext* engine = Core::gSharedEnginePtr;
            return engine ? engine->GetWindow() : nullptr;
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
            try { Key k; auto* in = Manager(); return in && !(Captured() & SHARD_CAPTURE_KEYBOARD) && MapKey(key, k) && in->IsKeyDown(k) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
        int32_t SHARD_CALL KeyPressed(int32_t key)
        {
            try { Key k; auto* in = Manager(); return in && !(Captured() & SHARD_CAPTURE_KEYBOARD) && MapKey(key, k) && in->WasKeyPressed(k) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
        int32_t SHARD_CALL KeyReleased(int32_t key)
        {
            try { Key k; auto* in = Manager(); return in && !(Captured() & SHARD_CAPTURE_KEYBOARD) && MapKey(key, k) && in->WasKeyReleased(k) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }

        int32_t SHARD_CALL MouseDown(int32_t button)
        {
            try { Input::MouseButton b; auto* in = Manager(); return in && !(Captured() & SHARD_CAPTURE_MOUSE) && MapButton(button, b) && in->IsMouseDown(b) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
        int32_t SHARD_CALL MousePressed(int32_t button)
        {
            try { Input::MouseButton b; auto* in = Manager(); return in && !(Captured() & SHARD_CAPTURE_MOUSE) && MapButton(button, b) && in->WasMousePressed(b) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
        int32_t SHARD_CALL MouseReleased(int32_t button)
        {
            try { Input::MouseButton b; auto* in = Manager(); return in && !(Captured() & SHARD_CAPTURE_MOUSE) && MapButton(button, b) && in->WasMouseReleased(b) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
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
            try { Input::GamepadButton b; auto* in = Manager(); return in && !(Captured() & SHARD_CAPTURE_GAMEPAD) && MapPadButton(button, b) && in->IsGamepadButtonDown(pad, b) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
        int32_t SHARD_CALL GamepadButtonPressed(int32_t pad, int32_t button)
        {
            try { Input::GamepadButton b; auto* in = Manager(); return in && !(Captured() & SHARD_CAPTURE_GAMEPAD) && MapPadButton(button, b) && in->WasGamepadButtonPressed(pad, b) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
        int32_t SHARD_CALL GamepadButtonReleased(int32_t pad, int32_t button)
        {
            try { Input::GamepadButton b; auto* in = Manager(); return in && !(Captured() & SHARD_CAPTURE_GAMEPAD) && MapPadButton(button, b) && in->WasGamepadButtonReleased(pad, b) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
        float SHARD_CALL GamepadAxis(int32_t pad, int32_t axis, float deadZone)
        {
            try {
                auto* in = Manager();
                if (!in || (Captured() & SHARD_CAPTURE_GAMEPAD) || axis < 0 || axis >= int(Input::GamepadAxis::Count)) return 0.0f;
                return in->GetGamepadAxis(pad, Input::GamepadAxis(axis), deadZone);
            } catch (...) { return 0.0f; }
        }
        int32_t SHARD_CALL GamepadRumble(int32_t pad, float low, float high, int32_t durationMs)
        {
            try { auto* in = Manager(); return in && in->SetGamepadRumble(pad, low, high, durationMs) ? SHARD_TRUE : SHARD_FALSE; } catch (...) { return SHARD_FALSE; }
        }
    }

    namespace {
        uint32_t SHARD_CALL EventCount()
        {
            gInputUsed = true;
            std::lock_guard<std::mutex> lock(gInputMutex);
            return uint32_t(gEvents.size());
        }

        ShardResult SHARD_CALL EventAt(uint32_t index, ShardInputEvent* out)
        {
            gInputUsed = true;
            try {
                if (!out || out->structSize < sizeof(uint32_t)) return SHARD_E_INVALID_ARG;
                std::lock_guard<std::mutex> lock(gInputMutex);
                if (index >= gEvents.size()) return SHARD_E_NOT_FOUND;
                const uint32_t asked = out->structSize;
                std::memcpy(out, &gEvents[index], std::min<size_t>(asked, sizeof(ShardInputEvent)));
                out->structSize = asked;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        uint32_t SHARD_CALL Modifiers()
        {
            gInputUsed = true;
            std::lock_guard<std::mutex> lock(gInputMutex);
            return gMods;
        }

        ShardResult SHARD_CALL WindowSize(int32_t* width, int32_t* height, float* dpiScale)
        {
            try {
                Core::Platform::IWindow* window = Window();
                if (!window) return SHARD_E_UNSUPPORTED;
                if (width) *width = window->GetFramebufferWidth();
                if (height) *height = window->GetFramebufferHeight();
                if (dpiScale) *dpiScale = window->GetContentScale();
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ClipboardGet(char* buffer, uint64_t capacity, uint64_t* size)
        {
            try {
                Core::Platform::IWindow* window = Window();
                if (!window || !window->GetNativeHandle()) return SHARD_E_UNSUPPORTED;      // no window system : no clipboard
                return CopyOut(Core::Platform::Clipboard::GetText(), buffer, capacity, size);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ClipboardSet(ShardStr text)
        {
            try {
                Core::Platform::IWindow* window = Window();
                if (!window || !window->GetNativeHandle()) return SHARD_E_UNSUPPORTED;
                Core::Platform::Clipboard::SetText(ToString(text));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetCursorShape(int32_t shape)
        {
            try {
                if (shape < SHARD_CURSOR_ARROW || shape > SHARD_CURSOR_NOT_ALLOWED) return SHARD_E_INVALID_ARG;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                Core::Platform::IWindow* window = Window();
                if (!window) return SHARD_E_UNSUPPORTED;
                window->SetCursorShape(shape);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetTextInput(int32_t enabled, int32_t x, int32_t y, int32_t width, int32_t height)
        {
            try {
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                Core::Platform::IWindow* window = Window();
                if (!window) return SHARD_E_UNSUPPORTED;
                window->SetTextInputActive(enabled != 0, x, y, width, height);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL CaptureInput(ShardPluginCtx ctx, uint32_t mask)
        {
            try {
                if (!FindContext(ctx)) return SHARD_E_STALE_HANDLE;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                gInputUsed = true;
                std::lock_guard<std::mutex> lock(gInputMutex);
                if (mask == 0) gCaptures.erase(PluginOwner(ctx));
                else gCaptures[PluginOwner(ctx)] = Capture{ mask & (SHARD_CAPTURE_MOUSE | SHARD_CAPTURE_KEYBOARD | SHARD_CAPTURE_GAMEPAD), gInputFrame };
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    void InputBeginFrame(uint64_t frame)
    {
        if (!gInputUsed.load()) return;
        // Listens to the window once there is one (the engine builds the platform before the first frame)
        if (!gListening) {
            if (Core::Platform::IWindow* window = Window()) {
                window->AddEventListener([](const Core::Platform::OSEvent& e) { try { OnOsEvent(e); } catch (...) {} });
                gListening = true;
            }
        }
        std::lock_guard<std::mutex> lock(gInputMutex);
        gInputFrame = frame;
        gEvents.clear();        // the events of this frame arrive during its Input phase
        for (auto it = gCaptures.begin(); it != gCaptures.end();) {
            if (it->second.frame + 1 < frame) it = gCaptures.erase(it); else ++it;
        }
    }

    void ReleaseInput(PluginOwner owner)
    {
        std::lock_guard<std::mutex> lock(gInputMutex);
        gCaptures.erase(owner);
    }

    const void* InputApi()
    {
        static const ShardInputApi api = {
            sizeof(ShardInputApi),
            &KeyDown, &KeyPressed, &KeyReleased,
            &MouseDown, &MousePressed, &MouseReleased, &GetCursor, &SetCursor, &SetCursorMode,
            &GamepadCount, &GamepadButtonDown, &GamepadButtonPressed, &GamepadButtonReleased, &GamepadAxis, &GamepadRumble,
            &EventCount, &EventAt, &Modifiers, &WindowSize, &ClipboardGet, &ClipboardSet, &SetCursorShape, &SetTextInput, &CaptureInput };
        return &api;
    }
}

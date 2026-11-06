#pragma once

#include <string>
#include <vector>

#include "engine/platform/devices/gamepad.hpp"
#include "engine/platform/devices/keys.hpp"

namespace Shard::Engine::Core::Platform {

    enum CursorVisibility{
        Visible,
        Hidden,
        Disabled
    };

    class IInput {
    public:
        virtual ~IInput() = default;
        virtual void Init() = 0;
        virtual void Tick() = 0;
        virtual void Shutdown() = 0;

        virtual bool IsKeyDown(Input::Key key) const = 0;
        
        virtual bool IsKeyUp(Input::Key key) const = 0;

        virtual bool WasKeyPressed(Input::Key key) const = 0;
        
        virtual bool WasKeyReleased(Input::Key key) const = 0;

        virtual bool IsMouseDown(Input::MouseButton button) const = 0;

        virtual bool IsMouseUp(Input::MouseButton button) const = 0;

        virtual bool WasMousePressed(Input::MouseButton button) const = 0;

        virtual bool WasMouseReleased(Input::MouseButton button) const = 0;

        virtual void SetCursorVisibility(CursorVisibility visible) const = 0;
        virtual void GetCursorPos(double* x, double* y) const = 0;
        virtual void SetCursorPos(double x, double y) const = 0;

        virtual int GetGamepadCount() const { return 0; }
        virtual std::string GetGamepadName(int pad) const { return ""; }

        virtual bool IsGamepadButtonDown(int pad, Input::GamepadButton button) const { return false; }
        virtual bool WasGamepadButtonPressed(int pad, Input::GamepadButton button) const { return false; }
        virtual bool WasGamepadButtonReleased(int pad, Input::GamepadButton button) const { return false; }

        // Sticks -1..1, triggers 0..1. Dead zone is a fraction of the range.
        virtual float GetGamepadAxis(int pad, Input::GamepadAxis axis, float deadZone = 0.1f) const { return 0.0f; }

        virtual bool SetGamepadRumble(int pad, float lowFrequency, float highFrequency, int durationMs) { return false; }
    };

}
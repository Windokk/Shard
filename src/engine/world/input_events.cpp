#include "engine/world/event_system.hpp"

namespace Shard::Engine::Events {

    bool TranslateInputEvent(const Core::Platform::OSEvent& event, EventDispatcher& dispatcher)
    {
        using Core::Platform::OSEventType;
        const Core::ObjectID source(0);

        switch (event.type)
        {
            case OSEventType::KeyDown:
                dispatcher.emitGlobal(KeyPressedEvent(static_cast<int>(event.key), event.repeat, source));
                return true;
            case OSEventType::KeyUp:
                dispatcher.emitGlobal(KeyReleasedEvent(static_cast<int>(event.key), source));
                return true;
            case OSEventType::TextInput:
                dispatcher.emitGlobal(TextInputEvent(event.text, source));
                return true;
            case OSEventType::MouseButtonDown:
                dispatcher.emitGlobal(MouseButtonPressedEvent(event.button, event.mouseX, event.mouseY, source));
                return true;
            case OSEventType::MouseButtonUp:
                dispatcher.emitGlobal(MouseButtonReleasedEvent(event.button, event.mouseX, event.mouseY, source));
                return true;
            case OSEventType::MouseMotion:
                dispatcher.emitGlobal(MouseMovedEvent(event.mouseX, event.mouseY, event.deltaX, event.deltaY, source));
                return true;
            case OSEventType::MouseWheel:
                dispatcher.emitGlobal(MouseScrolledEvent(event.deltaX, event.deltaY, source));
                return true;
            case OSEventType::GamepadConnected:
                dispatcher.emitGlobal(GamepadConnectedEvent(event.deviceId, source));
                return true;
            case OSEventType::GamepadDisconnected:
                dispatcher.emitGlobal(GamepadDisconnectedEvent(event.deviceId, source));
                return true;
            case OSEventType::GamepadButtonDown:
                dispatcher.emitGlobal(GamepadButtonPressedEvent(event.deviceId, event.gamepadButton, source));
                return true;
            case OSEventType::GamepadButtonUp:
                dispatcher.emitGlobal(GamepadButtonReleasedEvent(event.deviceId, event.gamepadButton, source));
                return true;
            case OSEventType::GamepadAxisMotion:
                dispatcher.emitGlobal(GamepadAxisEvent(event.deviceId, event.gamepadAxis, event.axisValue, source));
                return true;
            default:
                return false;
        }
    }
}

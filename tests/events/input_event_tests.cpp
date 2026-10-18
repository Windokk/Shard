#include <gtest/gtest.h>

#include "engine/world/event_system.hpp"

using namespace Shard::Engine::Events;
using namespace Shard::Engine::Input;
using Shard::Engine::Core::Platform::OSEvent;
using Shard::Engine::Core::Platform::OSEventType;

TEST(InputEvents, KeyDownBecomesKeyPressedWithRepeatFlag) {
    EventDispatcher dispatcher;
    int key = -1; bool repeated = false;
    dispatcher.subscribeGlobal<KeyPressedEvent>([&](const KeyPressedEvent& e) { key = e.keyCode; repeated = e.repeated; });

    OSEvent event; event.type = OSEventType::KeyDown; event.key = Key::W; event.repeat = true;
    EXPECT_TRUE(TranslateInputEvent(event, dispatcher));
    EXPECT_EQ(key, static_cast<int>(Key::W));
    EXPECT_TRUE(repeated);
}

TEST(InputEvents, KeyUpBecomesKeyReleased) {
    EventDispatcher dispatcher;
    Key key = Key::Space; int calls = 0;
    dispatcher.subscribeGlobal<KeyReleasedEvent>([&](const KeyReleasedEvent& e) { key = e.key(); ++calls; });

    OSEvent event; event.type = OSEventType::KeyUp; event.key = Key::Escape;
    EXPECT_TRUE(TranslateInputEvent(event, dispatcher));
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(key, Key::Escape);
}

TEST(InputEvents, MouseEventsCarryPositionAndMotion) {
    EventDispatcher dispatcher;
    float px = 0, dx = 0, scroll = 0; MouseButton button = MouseButton::Left;
    dispatcher.subscribeGlobal<MouseButtonPressedEvent>([&](const MouseButtonPressedEvent& e) { button = e.button; px = e.x; });
    dispatcher.subscribeGlobal<MouseMovedEvent>([&](const MouseMovedEvent& e) { dx = e.deltaX; });
    dispatcher.subscribeGlobal<MouseScrolledEvent>([&](const MouseScrolledEvent& e) { scroll = e.deltaY; });

    OSEvent down; down.type = OSEventType::MouseButtonDown; down.button = MouseButton::Right; down.mouseX = 12.0f;
    OSEvent move; move.type = OSEventType::MouseMotion; move.deltaX = 3.0f;
    OSEvent wheel; wheel.type = OSEventType::MouseWheel; wheel.deltaY = -1.0f;
    TranslateInputEvent(down, dispatcher); TranslateInputEvent(move, dispatcher); TranslateInputEvent(wheel, dispatcher);

    EXPECT_EQ(button, MouseButton::Right);
    EXPECT_FLOAT_EQ(px, 12.0f);
    EXPECT_FLOAT_EQ(dx, 3.0f);
    EXPECT_FLOAT_EQ(scroll, -1.0f);
}

TEST(InputEvents, GamepadEventsCarryTheDevice) {
    EventDispatcher dispatcher;
    uint32_t pad = 0; GamepadButton button = GamepadButton::South; float value = 0; int connected = 0;
    dispatcher.subscribeGlobal<GamepadConnectedEvent>([&](const GamepadConnectedEvent&) { ++connected; });
    dispatcher.subscribeGlobal<GamepadButtonPressedEvent>([&](const GamepadButtonPressedEvent& e) { pad = e.deviceId; button = e.button; });
    dispatcher.subscribeGlobal<GamepadAxisEvent>([&](const GamepadAxisEvent& e) { value = e.value; });

    OSEvent plug; plug.type = OSEventType::GamepadConnected; plug.deviceId = 7;
    OSEvent press; press.type = OSEventType::GamepadButtonDown; press.deviceId = 7; press.gamepadButton = GamepadButton::North;
    OSEvent axis; axis.type = OSEventType::GamepadAxisMotion; axis.deviceId = 7; axis.gamepadAxis = GamepadAxis::LeftTrigger; axis.axisValue = 0.5f;
    TranslateInputEvent(plug, dispatcher); TranslateInputEvent(press, dispatcher); TranslateInputEvent(axis, dispatcher);

    EXPECT_EQ(connected, 1);
    EXPECT_EQ(pad, 7u);
    EXPECT_EQ(button, GamepadButton::North);
    EXPECT_FLOAT_EQ(value, 0.5f);
}

TEST(InputEvents, NonInputEventsAreIgnored) {
    EventDispatcher dispatcher;
    OSEvent event; event.type = OSEventType::WindowResized;
    EXPECT_FALSE(TranslateInputEvent(event, dispatcher));
}

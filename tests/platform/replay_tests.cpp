#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "engine/platform/devices/keys.hpp"
#include "engine/platform/replay/input_replay.hpp"
#include "engine/platform/windowing/headless/headless_platform.hpp"

using namespace Shard::Engine::Core::Platform;
using namespace Shard::Engine::Input;

namespace {
    OSEvent KeyEvent(OSEventType type, Key key) {
        OSEvent e;
        e.type = type;
        e.key = key;
        return e;
    }

    bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }
}

// ---- names ------------------------------------------------------------------------------------------------------------

TEST(Replay, KeyNamesFollowTheEnum) {
    // Both ends and a few in the middle : the table in the replay module is written by hand, in the order of the enum
    EXPECT_STREQ(KeyName(Key::Space), "Space");
    EXPECT_STREQ(KeyName(Key::K0), "K0");
    EXPECT_STREQ(KeyName(Key::K9), "K9");
    EXPECT_STREQ(KeyName(Key::A), "A");
    EXPECT_STREQ(KeyName(Key::W), "W");
    EXPECT_STREQ(KeyName(Key::Z), "Z");
    EXPECT_STREQ(KeyName(Key::World2), "World2");
    EXPECT_STREQ(KeyName(Key::Escape), "Escape");
    EXPECT_STREQ(KeyName(Key::Pause), "Pause");
    EXPECT_STREQ(KeyName(Key::F1), "F1");
    EXPECT_STREQ(KeyName(Key::F25), "F25");
    EXPECT_STREQ(KeyName(Key::Keypad0), "Keypad0");
    EXPECT_STREQ(KeyName(Key::KeypadEqual), "KeypadEqual");
    EXPECT_STREQ(KeyName(Key::LeftShift), "LeftShift");
    EXPECT_STREQ(KeyName(Key::Menu), "Menu");

    for (int i = 0; i <= static_cast<int>(Key::Menu); ++i) {
        Key back;
        ASSERT_TRUE(KeyFromName(KeyName(static_cast<Key>(i)), back)) << i;
        EXPECT_EQ(static_cast<int>(back), i);
    }
    Key unused;
    EXPECT_FALSE(KeyFromName("NoSuchKey", unused));
}

// ---- the file ---------------------------------------------------------------------------------------------------------

TEST(Replay, EveryKindOfEventRoundTripsExactly) {
    ReplayData data;
    data.seed = 0xfedcba9876543210ull;
    data.fixedStep = 1.0f / 60.0f;
    data.world = "arena.world";
    data.frames = 500;

    OSEvent move;
    move.type = OSEventType::MouseMotion;
    move.mouseX = 123.456f; move.mouseY = -0.1f; move.deltaX = 1.0f / 3.0f; move.deltaY = -7.25f;
    OSEvent wheel;
    wheel.type = OSEventType::MouseWheel;
    wheel.deltaX = 0.0f; wheel.deltaY = -1.0f;
    OSEvent mouseDown;
    mouseDown.type = OSEventType::MouseButtonDown;
    mouseDown.button = MouseButton::Right;
    OSEvent text;
    text.type = OSEventType::TextInput;
    text.text = "h\xc3\xa9llo wor ld";            // a space and UTF-8 : none of it may break the line
    OSEvent padAxis;
    padAxis.type = OSEventType::GamepadAxisMotion;
    padAxis.deviceId = 3; padAxis.gamepadAxis = GamepadAxis::RightTrigger; padAxis.axisValue = 0.75f;
    OSEvent padDown;
    padDown.type = OSEventType::GamepadButtonDown;
    padDown.deviceId = 3; padDown.gamepadButton = GamepadButton::DpadLeft;
    OSEvent padConnected;
    padConnected.type = OSEventType::GamepadConnected;
    padConnected.deviceId = 3;
    OSEvent focus;
    focus.type = OSEventType::WindowFocusLost;

    data.events = {
        {0, padConnected}, {3, KeyEvent(OSEventType::KeyDown, Key::W)}, {3, move}, {4, wheel}, {5, mouseDown}, {5, text},
        {9, padAxis}, {9, padDown}, {12, focus}, {12, KeyEvent(OSEventType::KeyUp, Key::W)},
    };

    ReplayData::Check check;
    check.frames = 60; check.hash = 0x0123456789abcdefull; check.parts[0] = 1; check.parts[1] = 2; check.parts[2] = 0xffffffffffffffffull; check.hasParts = true;
    ReplayData::Check plain;
    plain.frames = 120; plain.hash = 0xabcull;
    data.checks = {check, plain};

    ReplayData back;
    std::string error;
    ASSERT_TRUE(ReplayData::Parse(data.Serialize(), back, error)) << error;

    EXPECT_EQ(back.seed, data.seed);
    EXPECT_TRUE(SameBits(back.fixedStep, data.fixedStep));
    EXPECT_EQ(back.world, data.world);
    EXPECT_EQ(back.frames, 500u);
    ASSERT_EQ(back.events.size(), data.events.size());
    for (size_t i = 0; i < back.events.size(); ++i) {
        EXPECT_EQ(back.events[i].frame, data.events[i].frame) << i;
        EXPECT_EQ(back.events[i].event.type, data.events[i].event.type) << i;
    }
    EXPECT_EQ(back.events[1].event.key, Key::W);
    EXPECT_TRUE(SameBits(back.events[2].event.mouseX, 123.456f));
    EXPECT_TRUE(SameBits(back.events[2].event.mouseY, -0.1f));
    EXPECT_TRUE(SameBits(back.events[2].event.deltaX, 1.0f / 3.0f)) << "floats go by their bits, not by a decimal that might round";
    EXPECT_EQ(back.events[3].event.deltaY, -1.0f);
    EXPECT_EQ(back.events[4].event.button, MouseButton::Right);
    EXPECT_EQ(back.events[5].event.text, text.text);
    EXPECT_EQ(back.events[6].event.gamepadAxis, GamepadAxis::RightTrigger);
    EXPECT_EQ(back.events[6].event.axisValue, 0.75f);
    EXPECT_EQ(back.events[7].event.gamepadButton, GamepadButton::DpadLeft);
    EXPECT_EQ(back.events[7].event.deviceId, 3u);

    ASSERT_EQ(back.checks.size(), 2u);
    EXPECT_EQ(back.checks[0].hash, check.hash);
    EXPECT_TRUE(back.checks[0].hasParts);
    EXPECT_EQ(back.checks[0].parts[2], 0xffffffffffffffffull);
    EXPECT_FALSE(back.checks[1].hasParts);
}

TEST(Replay, AHandWrittenFileReadsWithDecimalsAndComments) {
    const std::string text =
        "shard-replay 1\r\n"
        "# a comment\r\n"
        "\r\n"
        "seed 7\r\n"
        "fixed-step 0.0166666675\r\n"
        "@10 key-down LeftShift\r\n"
        "@10 mouse-move 100 50.5 -2 0\r\n"
        "@11 mouse-up 0\r\n"
        "check 30 ff\r\n";
    ReplayData data;
    std::string error;
    ASSERT_TRUE(ReplayData::Parse(text, data, error)) << error;
    EXPECT_EQ(data.seed, 7u);
    EXPECT_NEAR(data.fixedStep, 1.0f / 60.0f, 1e-8f);
    ASSERT_EQ(data.events.size(), 3u);
    EXPECT_EQ(data.events[0].event.key, Key::LeftShift);
    EXPECT_EQ(data.events[1].event.mouseY, 50.5f);
    EXPECT_EQ(data.checks[0].hash, 0xffu);
    EXPECT_EQ(data.Length(), 30u) << "no frames line : the last check says how long the run is";
}

TEST(Replay, BadFilesAreRefusedWithTheLine) {
    ReplayData data;
    std::string error;

    EXPECT_FALSE(ReplayData::Parse("", data, error));
    EXPECT_FALSE(ReplayData::Parse("hello 1\n", data, error));
    EXPECT_FALSE(ReplayData::Parse("shard-replay 99\n", data, error));
    EXPECT_NE(error.find("version"), std::string::npos);

    EXPECT_FALSE(ReplayData::Parse("shard-replay 1\n@3 key-down NoSuchKey\n", data, error));
    EXPECT_NE(error.find("line 2"), std::string::npos) << error;
    EXPECT_NE(error.find("NoSuchKey"), std::string::npos) << error;

    EXPECT_FALSE(ReplayData::Parse("shard-replay 1\n@9 key-down W\n@3 key-up W\n", data, error));
    EXPECT_NE(error.find("order"), std::string::npos) << error;

    EXPECT_FALSE(ReplayData::Parse("shard-replay 1\ncheck 10 nothex\n", data, error));
    EXPECT_FALSE(ReplayData::Parse("shard-replay 1\n@1 mouse-down 12\n", data, error));
    EXPECT_FALSE(ReplayData::Parse("shard-replay 1\nwhatever\n", data, error));
}

// ---- recording and playing --------------------------------------------------------------------------------------------

TEST(Replay, TheRecorderStampsTheFrameAndSkipsWhatIsNotInput) {
    HeadlessWindow window;
    window.Init("test", 640, 360, false, 0, 0);

    ReplayRecorder recorder;
    recorder.Attach(window);

    recorder.SetFrame(4);
    window.PostEvent(KeyEvent(OSEventType::KeyDown, Key::A));
    OSEvent repeat = KeyEvent(OSEventType::KeyDown, Key::A);
    repeat.repeat = true;
    window.PostEvent(repeat);
    OSEvent resized;
    resized.type = OSEventType::WindowResized;
    resized.width = 800; resized.height = 600;
    window.PostEvent(resized);
    window.PollEvents();

    recorder.SetFrame(5);
    window.PostEvent(KeyEvent(OSEventType::KeyUp, Key::A));
    window.PollEvents();

    ASSERT_EQ(recorder.Data().events.size(), 2u) << "the key repeat and the resize are not part of a replay";
    EXPECT_EQ(recorder.Data().events[0].frame, 4u);
    EXPECT_EQ(recorder.Data().events[1].frame, 5u);
    EXPECT_EQ(recorder.Data().events[1].event.type, OSEventType::KeyUp);

    recorder.Detach();
    window.PostEvent(KeyEvent(OSEventType::KeyDown, Key::B));
    window.PollEvents();
    EXPECT_EQ(recorder.Data().events.size(), 2u);
}

TEST(Replay, ThePlayerDeliversEachFrameItsEvents) {
    ReplayData data;
    data.events = {{0, KeyEvent(OSEventType::KeyDown, Key::W)}, {2, KeyEvent(OSEventType::KeyUp, Key::W)}, {2, KeyEvent(OSEventType::KeyDown, Key::S)}, {7, KeyEvent(OSEventType::KeyUp, Key::S)}};
    ReplayData::Check check;
    check.frames = 5; check.hash = 99;
    data.checks = {check};

    ReplayPlayer player(data);
    std::vector<std::pair<uint64_t, Key>> delivered;
    for (uint64_t frame = 0; frame < 9; ++frame)
        player.Deliver(frame, [&](const OSEvent& e) { delivered.push_back({frame, e.key}); });

    ASSERT_EQ(delivered.size(), 4u);
    EXPECT_EQ(delivered[0], (std::pair<uint64_t, Key>{0, Key::W}));
    EXPECT_EQ(delivered[1], (std::pair<uint64_t, Key>{2, Key::W}));
    EXPECT_EQ(delivered[2], (std::pair<uint64_t, Key>{2, Key::S}));
    EXPECT_EQ(delivered[3], (std::pair<uint64_t, Key>{7, Key::S}));

    ASSERT_NE(player.CheckAfter(5), nullptr);
    EXPECT_EQ(player.CheckAfter(5)->hash, 99u);
    EXPECT_EQ(player.CheckAfter(6), nullptr);
}

TEST(Replay, AReplayDrivesTheHeadlessInputLikeAKeyboard) {
    HeadlessPlatform platform;
    platform.CreateWindow("test", 640, 360, false, 0, 0);
    platform.CreateInput();
    IInput* input = platform.GetInput();

    ReplayData data;
    data.events = {{1, KeyEvent(OSEventType::KeyDown, Key::W)}, {3, KeyEvent(OSEventType::KeyUp, Key::W)}};
    ReplayPlayer player(data);

    bool down[5], pressed[5], released[5];
    for (uint64_t frame = 0; frame < 5; ++frame) {
        player.Deliver(frame, [&](const OSEvent& e) { platform.GetHeadlessWindow()->PostEvent(e); });
        // The order of the engine's input phase : the previous state is kept, then the events of the frame are read
        input->Tick();
        platform.GetWindow()->PollEvents();
        down[frame] = input->IsKeyDown(Key::W);
        pressed[frame] = input->WasKeyPressed(Key::W);
        released[frame] = input->WasKeyReleased(Key::W);
    }

    EXPECT_FALSE(down[0]);
    EXPECT_TRUE(down[1]);   EXPECT_TRUE(pressed[1]);   EXPECT_FALSE(released[1]);
    EXPECT_TRUE(down[2]);   EXPECT_FALSE(pressed[2]);
    EXPECT_FALSE(down[3]);  EXPECT_TRUE(released[3]);
    EXPECT_FALSE(down[4]);  EXPECT_FALSE(released[4]);
}

// ---- the headless window ----------------------------------------------------------------------------------------------

TEST(HeadlessPlatform, TheWindowHasASizeAndClosesOnQuit) {
    HeadlessPlatform platform;
    platform.CreateWindow("server", 1280, 720, false, 0, 0);
    IWindow* window = platform.GetWindow();
    EXPECT_EQ(window->GetFramebufferWidth(), 1280);
    EXPECT_EQ(window->GetFramebufferHeight(), 720);
    EXPECT_FALSE(window->ShouldClose());

    OSEvent resize;
    resize.type = OSEventType::WindowResized;
    resize.width = 640; resize.height = 480;
    platform.GetHeadlessWindow()->PostEvent(resize);
    window->PollEvents();
    EXPECT_EQ(window->GetFramebufferWidth(), 640);

    OSEvent quit;
    quit.type = OSEventType::Quit;
    platform.GetHeadlessWindow()->PostEvent(quit);
    window->PollEvents();
    EXPECT_TRUE(window->ShouldClose());
    EXPECT_EQ(window->GetSystemInfos().windowHost, WindowHost::Headless);
}

TEST(HeadlessPlatform, MouseAndGamepadStateFollowTheEvents) {
    HeadlessPlatform platform;
    platform.CreateWindow("test", 640, 360, false, 0, 0);
    platform.CreateInput();
    IInput* input = platform.GetInput();
    HeadlessWindow* window = platform.GetHeadlessWindow();

    OSEvent move;
    move.type = OSEventType::MouseMotion;
    move.mouseX = 30.0f; move.mouseY = 40.0f; move.deltaX = 3.0f; move.deltaY = 4.0f;
    OSEvent press;
    press.type = OSEventType::MouseButtonDown;
    press.button = MouseButton::Left;
    OSEvent connect;
    connect.type = OSEventType::GamepadConnected;
    connect.deviceId = 11;
    OSEvent axis;
    axis.type = OSEventType::GamepadAxisMotion;
    axis.deviceId = 11; axis.gamepadAxis = GamepadAxis::LeftX; axis.axisValue = 0.5f;
    for (const OSEvent& e : {move, press, connect, axis}) window->PostEvent(e);
    window->PollEvents();

    double x = 0, y = 0;
    input->GetCursorPos(&x, &y);
    EXPECT_EQ(x, 30.0);
    EXPECT_EQ(y, 40.0);
    EXPECT_TRUE(input->IsMouseDown(MouseButton::Left));
    EXPECT_EQ(input->GetGamepadCount(), 1);
    EXPECT_EQ(input->GetGamepadAxis(0, GamepadAxis::LeftX), 0.5f);
    EXPECT_EQ(input->GetGamepadAxis(0, GamepadAxis::LeftY), 0.0f);

    // Captured : the cursor moves by the relative motion
    input->SetCursorVisibility(CursorVisibility::Disabled);
    window->PostEvent(move);
    window->PollEvents();
    input->GetCursorPos(&x, &y);
    EXPECT_EQ(x, 33.0);
    EXPECT_EQ(y, 44.0);
}

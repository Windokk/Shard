#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <set>

#include "engine/platform/atomic/atomic.hpp"
#include "engine/platform/devices/sdl/sdl_input.hpp"
#include "engine/platform/devices/sdl/sdl_keymap.hpp"
#include "engine/platform/events/os_event.hpp"
#include "engine/platform/hardware/system_info.hpp"
#include "engine/platform/thread/thread.hpp"
#include "engine/platform/time/clock.hpp"

using namespace Shard::Engine::Core::Platform;
using Shard::Engine::Input::Key;

// --- time ---

TEST(PlatformTime, ClockIsMonotonic)
{
    uint64_t previous = NowNanoseconds();
    for (int i = 0; i < 1000; ++i)
    {
        const uint64_t now = NowNanoseconds();
        ASSERT_GE(now, previous);
        previous = now;
    }
}

TEST(PlatformTime, PreciseSleepNeverReturnsEarlyAndIsCloseToTheRequest)
{
    for (const double seconds : { 0.001, 0.005, 0.016 })
    {
        const double start = NowSeconds();
        PreciseSleep(seconds);
        const double elapsed = NowSeconds() - start;

        EXPECT_GE(elapsed, seconds - 1e-6) << "returned early for " << seconds << " s";   // 1 us of rounding of the double clock
        // loose upper bound : a loaded CI machine can deschedule the thread
        EXPECT_LT(elapsed, seconds + 0.05) << "overslept for " << seconds << " s";
    }
}

TEST(PlatformTime, LocalTimeIsACalendarTime)
{
    const LocalTime t = GetLocalTime();
    EXPECT_GE(t.year, 2024);
    EXPECT_GE(t.month, 1);  EXPECT_LE(t.month, 12);
    EXPECT_GE(t.day, 1);    EXPECT_LE(t.day, 31);
    EXPECT_GE(t.dayOfYear, 0);  EXPECT_LE(t.dayOfYear, 365);
    EXPECT_GE(t.hour, 0);   EXPECT_LE(t.hour, 23);
    EXPECT_GE(t.minute, 0); EXPECT_LE(t.minute, 59);
    EXPECT_GE(t.second, 0); EXPECT_LE(t.second, 61);
    EXPECT_GE(t.millisecond, 0); EXPECT_LE(t.millisecond, 999);
}

// --- threads / atomics ---

TEST(PlatformThread, HardwareConcurrencyIsAtLeastOneAndWorkersLeaveTheCallingThread)
{
    EXPECT_GE(HardwareConcurrency(), 1u);
    EXPECT_GE(WorkerThreadCount(), 1u);
    EXPECT_LE(WorkerThreadCount(), HardwareConcurrency());
}

TEST(PlatformThread, ThreadRunsItsFunctionOnAnotherThreadAndJoinsOnDestruction)
{
    std::atomic<uint64_t> threadId{ 0 };
    std::atomic<bool> ran{ false };
    {
        ThreadDesc desc;
        desc.name = "test thread";
        desc.priority = ThreadPriority::Low;
        Thread thread(desc, [&] { threadId = CurrentThreadId(); ran = true; });
    }
    EXPECT_TRUE(ran);
    EXPECT_NE(threadId.load(), CurrentThreadId());
}

TEST(PlatformThread, AffinityToTheFirstCpuIsAccepted)
{
    // Pins a throw-away thread : the main one must keep running everywhere
    bool accepted = false;
    {
        Thread thread({}, [&] { accepted = SetCurrentThreadAffinity(1); });
    }
#if defined(__APPLE__)
    EXPECT_FALSE(accepted);
#else
    EXPECT_TRUE(accepted);
#endif
}

TEST(PlatformAtomic, SpinLockProtectsASharedCounter)
{
    SpinLock lock;
    long long counter = 0;
    constexpr int kThreads = 4, kIterations = 20000;
    {
        std::vector<Thread> threads;
        for (int t = 0; t < kThreads; ++t)
            threads.emplace_back(ThreadDesc{}, [&] {
                for (int i = 0; i < kIterations; ++i)
                {
                    std::lock_guard<SpinLock> guard(lock);
                    ++counter;
                }
            });
    }
    EXPECT_EQ(counter, static_cast<long long>(kThreads) * kIterations);
}

TEST(PlatformAtomic, CacheLinePaddedValuesDoNotShareACacheLine)
{
    EXPECT_GE(sizeof(CacheLinePadded<int>), kCacheLineSize);
    EXPECT_EQ(alignof(CacheLinePadded<int>), kCacheLineSize);
}

// --- hardware ---

TEST(PlatformHardware, DescribesTheMachine)
{
    const SystemInfo& info = GetSystemInfo();
    EXPECT_GE(info.cpu.logicalCores, 1u);
    EXPECT_GE(info.cpu.logicalCores, info.cpu.physicalCores);
    EXPECT_GT(info.memory.totalPhysical, 0u);
    EXPECT_FALSE(info.os.name.empty());
    EXPECT_FALSE(info.os.arch.empty());
#if defined(__x86_64__) || defined(_M_X64)
    EXPECT_TRUE(info.cpu.features.sse2);    // part of x86-64
    EXPECT_FALSE(info.cpu.brand.empty());
#endif
}

TEST(PlatformHardware, MemoryNumbersAreCoherent)
{
    const MemoryInfo memory = QueryMemory();
    EXPECT_GT(memory.availablePhysical, 0u);
    EXPECT_LE(memory.availablePhysical, memory.totalPhysical);
    EXPECT_GT(ProcessMemoryUsage(), 0u);
    EXPECT_NO_THROW(ProcessGpuMemoryUsage());   // 0 is fine : the process has no GPU memory
}

// --- keys / events ---

TEST(PlatformDevices, EveryKeyThatHasAScancodeRoundTrips)
{
    int mapped = 0;
    for (int i = 0; i <= static_cast<int>(Key::Menu); ++i)
    {
        const Key key = static_cast<Key>(i);
        const SDL_Scancode scancode = KeyToScancode(key);
        if (scancode == SDL_SCANCODE_UNKNOWN)
            continue;

        Key back;
        ASSERT_TRUE(ScancodeToKey(scancode, back)) << "key " << i;
        EXPECT_EQ(static_cast<int>(back), i);
        ++mapped;
    }
    // Everything but F25
    EXPECT_EQ(mapped, static_cast<int>(Key::Menu) + 1 - 1);
}

TEST(PlatformDevices, LayoutIndependentLetters)
{
    EXPECT_EQ(KeyToScancode(Key::W), SDL_SCANCODE_W);
    EXPECT_EQ(KeyToScancode(Key::Z), SDL_SCANCODE_Z);
    EXPECT_EQ(KeyToScancode(Key::N), SDL_SCANCODE_N);   // the old backend only knew some letters
    Key key;
    EXPECT_FALSE(ScancodeToKey(SDL_SCANCODE_UNKNOWN, key));
}

namespace {
    struct TestSource : OSEventSource {
        void Emit(OSEventType type) { OSEvent e; e.type = type; DispatchEvent(e); }
    };
}

TEST(PlatformEvents, ListenersSeeEventsInOrderAndCanBeRemoved)
{
    TestSource source;
    std::vector<OSEventType> first, second;
    const auto id1 = source.AddEventListener([&](const OSEvent& e) { first.push_back(e.type); });
    source.AddEventListener([&](const OSEvent& e) { second.push_back(e.type); });

    source.Emit(OSEventType::KeyDown);
    source.Emit(OSEventType::KeyUp);
    source.RemoveEventListener(id1);
    source.Emit(OSEventType::WindowResized);

    EXPECT_EQ(first, (std::vector<OSEventType>{ OSEventType::KeyDown, OSEventType::KeyUp }));
    EXPECT_EQ(second, (std::vector<OSEventType>{ OSEventType::KeyDown, OSEventType::KeyUp, OSEventType::WindowResized }));
}

TEST(PlatformEvents, AListenerCanRegisterAnotherOneWhileItRuns)
{
    TestSource source;
    int late = 0;
    bool added = false;
    source.AddEventListener([&](const OSEvent&) {
        if (!added) { added = true; source.AddEventListener([&](const OSEvent&) { ++late; }); }
    });

    source.Emit(OSEventType::KeyDown);
    source.Emit(OSEventType::KeyDown);
    EXPECT_EQ(late, 2);     // the new listener is called from the event that was being dispatched when it was added
}

// --- gamepads ---

TEST(PlatformDevices, GamepadButtonsAndAxesFollowSDLsLayout)
{
    Shard::Engine::Input::GamepadButton button;
    ASSERT_TRUE(SDLGamepadButtonToButton(SDL_GAMEPAD_BUTTON_SOUTH, button));
    EXPECT_EQ(button, Shard::Engine::Input::GamepadButton::South);
    ASSERT_TRUE(SDLGamepadButtonToButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT, button));
    EXPECT_EQ(button, Shard::Engine::Input::GamepadButton::DpadLeft);
    EXPECT_FALSE(SDLGamepadButtonToButton(SDL_GAMEPAD_BUTTON_TOUCHPAD, button));   // not exposed

    Shard::Engine::Input::GamepadAxis axis;
    ASSERT_TRUE(SDLGamepadAxisToAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, axis));
    EXPECT_EQ(axis, Shard::Engine::Input::GamepadAxis::RightTrigger);
    EXPECT_FALSE(SDLGamepadAxisToAxis(SDL_GAMEPAD_AXIS_COUNT, axis));
}

TEST(PlatformDevices, InputWithoutGamepadsReportsNoneAndNeutralValues)
{
    SDLWindow window;   // not initialised : no video, no device
    SDLInput input(&window);
    input.Init();

    EXPECT_EQ(input.GetGamepadCount(), 0);
    EXPECT_FALSE(input.IsGamepadButtonDown(0, Shard::Engine::Input::GamepadButton::South));
    EXPECT_FALSE(input.WasGamepadButtonPressed(0, Shard::Engine::Input::GamepadButton::South));
    EXPECT_EQ(input.GetGamepadAxis(0, Shard::Engine::Input::GamepadAxis::LeftX), 0.0f);
    EXPECT_EQ(input.GetGamepadName(-1), "");
    EXPECT_FALSE(input.SetGamepadRumble(0, 1.0f, 1.0f, 100));
    input.Shutdown();
}

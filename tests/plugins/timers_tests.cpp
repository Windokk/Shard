#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include <shard/shard.h>

#include "engine/core/ecs/registry.hpp"
#include "engine/core/jobs/job_system.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/plugins/wasm/wasm_plugin.hpp"
#include "plugin_fixtures/wasm_plugin/wasm_stats.h"
#include "engine/world/actor.hpp"
#include "engine/world/time_manager.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"
#include "plugins/asset_test_context.hpp"

#include "config.h"

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;

// shard.timers through a real plugin (tests/plugin_fixtures/game_scripts/src/timed.cpp), on a world the world manager holds : the C ABI, the
// C++ SDK on top of it, and the release when the plugin goes.

namespace {
    const char* kId = "shard.test.gamescripts";
    constexpr int kStepsPerSecond = 60;

    class TimersContext : public PluginTest::AssetContext {
    public:
        TimersContext() { time.Init(1.0f / float(kStepsPerSecond)); }
        Time::TimeManager* GetTimeManager() const override { return const_cast<Time::TimeManager*>(&time); }
        Rendering::IRenderContext* GetRenderContext() const override { return nullptr; }
        Worlds::WorldManager* GetWorldManager() const override { return const_cast<Worlds::WorldManager*>(&worlds); }
        Time::TimeManager time;
        Worlds::WorldManager worlds;
    };

    class TimersTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            PluginManager::Get().ResetForTests();
            config.host = Host::Headless;
            config.registerConsoleCommands = false;
            config.pluginPaths.push_back(std::string(SHARD_TEST_PLUGIN_DIR) + "/" + kId);
            Core::JobSystemDesc desc;
            desc.workerCount = 2;
            jobs = std::make_unique<Core::JobSystem>(desc);
            context.jobSystem = jobs.get();
            Core::SetEngine(&context);
            // Loading a world reaches the physics
            physics.Init(glm::vec3(0.0f, -9.81f, 0.0f));
            context.physicsManager = &physics;
        }
        void TearDown() override
        {
            if (world) world->Stop();
            world.reset();
            PluginManager::Get().ResetForTests();
            physics.Shutdown();
            context.physicsManager = nullptr;
            jobs.reset();
        }

        PluginManager& pm() { return PluginManager::Get(); }
        PluginRecord* Rec() { return pm().Find(pluginId); }
        int Stat(const char* name) { return Rec()->library.Symbol<int (*)(const char*)>("game_stat")(name); }

        void Start()
        {
            std::string error;
            ASSERT_TRUE(pm().Startup(config, &error)) << error;
            ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
            pm().OnEngineSystemsReady(&context, &scheduler, &events);
            pm().OnEngineInitialized();
            ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
            world = std::make_shared<Worlds::World>("w", Filesystem::Path("w.world"));
            context.worlds.LoadWorld(world);
        }

        void PlayWithTimedScript()
        {
            auto actor = Core::Object::CreateWithContext<Objects::Actor>(&context, "Timed", &context);
            world->AddActor(actor);
            ASSERT_NE(actor->AddComponentByName(std::string(kId) + ".Timed"), nullptr);
            world->Play();
        }

        void Steps(int count)
        {
            for (int i = 0; i < count; i++) world->FixedTick();
        }

        // Declared first so it is destroyed last : the world manager of the context needs the engine to destroy itself
        struct EngineReset { ~EngineReset() { Core::SetEngine(nullptr); } } engineReset;
        std::string pluginId = kId;
        PluginManagerConfig config;
        TimersContext context;
        std::unique_ptr<Core::JobSystem> jobs;
        Physics::PhysicsManager physics;
        Core::Ecs::Scheduler scheduler;
        Events::EventDispatcher events;
        std::shared_ptr<Worlds::World> world;
    };
}

TEST_F(TimersTest, DelaysAndRepeatsRunInFixedSteps)
{
    Start();
    PlayWithTimedScript();

    Steps(kStepsPerSecond / 2 - 1);
    EXPECT_EQ(Stat("timerOnce"), 0);
    EXPECT_EQ(Stat("timerEvery"), 1);           // 0.25 s
    Steps(1);
    EXPECT_EQ(Stat("timerOnce"), 1);            // 0.5 s, on the step that reaches it
    EXPECT_EQ(Stat("timerEvery"), 2);

    Steps(kStepsPerSecond * 5);
    EXPECT_EQ(Stat("timerOnce"), 1);            // once only
    EXPECT_EQ(Stat("timerEvery"), 2 + 20);
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(TimersTest, CoroutineWaitsOnATimeAndACondition)
{
    Start();
    PlayWithTimedScript();

    Steps(1);
    EXPECT_EQ(Stat("coroSteps"), 1);
    Steps(kStepsPerSecond - 2);
    EXPECT_EQ(Stat("coroSteps"), 1);
    Steps(2);
    EXPECT_EQ(Stat("coroSteps"), 2);            // after 1 s, now polling the condition

    Steps(kStepsPerSecond);
    EXPECT_EQ(Stat("coroDone"), 0);
}

TEST_F(TimersTest, SdkUntilTimesOutWithoutPhysicsSteps)
{
    // The coroutine waits 1 s, then on a condition with a 5 s time-out. The physics never steps here : the time-out is counted by the SDK itself.
    Start();
    PlayWithTimedScript();
    Steps(kStepsPerSecond + 2);
    Steps(kStepsPerSecond * 5 - 5);
    EXPECT_EQ(Stat("coroDone"), 0);
    Steps(10);
    EXPECT_EQ(Stat("coroDone"), 1);
}

TEST_F(TimersTest, EditTickDoesNotSchedule)
{
    Start();
    auto actor = Core::Object::CreateWithContext<Objects::Actor>(&context, "Timed", &context);
    world->AddActor(actor);
    ASSERT_NE(actor->AddComponentByName(std::string(kId) + ".Timed"), nullptr);

    {
        Worlds::Scheduler::RefuseScope edit;
        world->Play();                          // OnPlay makes timers : refused, and the SDK reports it without crashing
    }
    EXPECT_EQ(world->GetScheduler().Count(), 0u);
}

TEST_F(TimersTest, ConditionResumesTheCoroutine)
{
    Start();
    PlayWithTimedScript();
    Steps(kStepsPerSecond + 2);

    Rec()->library.Symbol<void (*)()>("game_raise_until")();
    Steps(2);
    EXPECT_EQ(Stat("coroDone"), 1);
}

TEST_F(TimersTest, EventLatchResumesAndRemembersTheEvent)
{
    Start();
    PlayWithTimedScript();

    Steps(5);
    EXPECT_EQ(Stat("eventResumed"), 0);
    events.emitNamed("timed.go", nullptr, 0);
    Steps(2);
    EXPECT_EQ(Stat("eventResumed"), 1);
    EXPECT_EQ(Stat("eventFired"), 1);           // it was the event, not the time-out
}

TEST_F(TimersTest, EventWaitTimesOut)
{
    Start();
    PlayWithTimedScript();

    Steps(kStepsPerSecond * 3 + 2);
    EXPECT_EQ(Stat("eventResumed"), 1);
    EXPECT_EQ(Stat("eventFired"), 0);
}

TEST_F(TimersTest, TimersDieWithTheWorldRun)
{
    Start();
    PlayWithTimedScript();
    Steps(kStepsPerSecond);
    const int every = Stat("timerEvery");
    EXPECT_GT(world->GetScheduler().Count(), 0u);

    world->Stop();
    EXPECT_EQ(world->GetScheduler().Count(), 0u);
    Steps(kStepsPerSecond);
    EXPECT_EQ(Stat("timerEvery"), every);
}

TEST_F(TimersTest, NoCallbackIntoADisabledPlugin)
{
    Start();
    PlayWithTimedScript();
    Steps(kStepsPerSecond / 4);
    EXPECT_EQ(Stat("timerEvery"), 1);

    // The plugin's context goes (what disabling does) : its timers must not be called any more, the library may be unloading
    pm().ResetForTests();
    world->FixedTick();
    EXPECT_EQ(world->GetScheduler().Count(), 0u);
    world->Stop();
    world.reset();
}

#ifdef SHARD_TEST_WASM_PLUGIN_DIR
// The same through the sandbox : the plugin is a WebAssembly module built from tests/plugin_fixtures/wasm_plugin/wasm_plugin.c, its callbacks are
// entries of its function table that the host reaches through trampolines.
class WasmTimersTest : public TimersTest {
protected:
    void SetUp() override
    {
        TimersTest::SetUp();
        config.pluginPaths.clear();
        config.pluginPaths.push_back(SHARD_TEST_WASM_PLUGIN_DIR);
        pluginId = "shard.test.wasm";
    }

    uint64_t Call(const char* name)
    {
        std::vector<uint64_t> results;
        std::string error;
        EXPECT_TRUE(CallWasmExport(Rec()->owner, name, {}, results, &error)) << error;
        return results.empty() ? 0 : results[0];
    }
    int Stat(int which)
    {
        std::vector<uint64_t> results;
        std::string error;
        EXPECT_TRUE(CallWasmExport(Rec()->owner, "wasm_stat", { uint64_t(which) }, results, &error)) << error;
        return results.empty() ? -1 : int(int32_t(uint32_t(results[0])));
    }
    int Result(const char* name) { return int(int32_t(uint32_t(Call(name)))); }
    void StartTimers()
    {
        Start();
        ASSERT_EQ(Result("wasm_timers_start"), SHARD_OK);
        world->Play();
    }
};

TEST_F(WasmTimersTest, TimersAndCoroutineRunInTheModule)
{
    StartTimers();

    Steps(kStepsPerSecond / 2);
    EXPECT_EQ(Stat(S_TIMER_ONCE), 1);
    EXPECT_EQ(Stat(S_TIMER_EVERY), 2);
    EXPECT_EQ(Stat(S_CORO_STEPS), 1);

    Steps(kStepsPerSecond / 2);
    EXPECT_EQ(Stat(S_CORO_STEPS), 2);           // after a second, now waiting on the latch
    EXPECT_EQ(Stat(S_CORO_DONE), 0);

    events.emitNamed("wasm.go", nullptr, 0);
    Steps(2);
    EXPECT_EQ(Stat(S_CORO_DONE), 1);
    EXPECT_EQ(Stat(S_LATCH_FIRED), 1);
    EXPECT_EQ(Stat(S_TIMER_ONCE), 1);
    EXPECT_EQ(Stat(S_FAILURES), 0);
}

TEST_F(WasmTimersTest, LatchTimesOut)
{
    StartTimers();
    Steps(kStepsPerSecond * 4 + 2);
    EXPECT_EQ(Stat(S_CORO_DONE), 1);
    EXPECT_EQ(Stat(S_LATCH_FIRED), 0);
}

TEST_F(WasmTimersTest, CancelStopsAndReportsTheSecondTime)
{
    StartTimers();
    Steps(kStepsPerSecond / 4);
    EXPECT_EQ(Stat(S_TIMER_EVERY), 1);

    EXPECT_EQ(Result("wasm_timers_active"), SHARD_TRUE);
    EXPECT_EQ(Result("wasm_timers_cancel_every"), SHARD_OK);
    EXPECT_EQ(Result("wasm_timers_cancel_every"), SHARD_E_NOT_FOUND);
    EXPECT_EQ(Result("wasm_timers_active"), SHARD_FALSE);
    Steps(kStepsPerSecond);
    EXPECT_EQ(Stat(S_TIMER_EVERY), 1);
    EXPECT_EQ(Stat(S_TIMER_ONCE), 1);           // the others went on
}

TEST_F(WasmTimersTest, CancelAllAndRestart)
{
    StartTimers();
    Steps(kStepsPerSecond / 4);
    EXPECT_EQ(Result("wasm_timers_cancel_all"), SHARD_OK);
    Steps(kStepsPerSecond * 2);
    EXPECT_EQ(Stat(S_TIMER_ONCE), 0);
    EXPECT_EQ(Stat(S_TIMER_EVERY), 1);

    EXPECT_EQ(Result("wasm_timers_start"), SHARD_OK);   // the records of the cancelled ones were dropped : starting again works
    Steps(kStepsPerSecond / 2);
    EXPECT_EQ(Stat(S_TIMER_ONCE), 1);
}

TEST_F(WasmTimersTest, NoCallbackIntoADisabledSandbox)
{
    StartTimers();
    Steps(kStepsPerSecond / 4);
    pm().ResetForTests();
    world->FixedTick();
    EXPECT_EQ(world->GetScheduler().Count(), 0u);
    world->Stop();
    world.reset();
}
#endif

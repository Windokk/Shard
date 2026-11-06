#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "engine/core/config/cvar.hpp"
#include "engine/core/ecs/registry.hpp"
#include "engine/platform/devices/iinput.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/plugins/wasm/wasm_plugin.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/time_manager.hpp"
#include "engine/world/world.hpp"
#include "plugin_fixtures/wasm_plugin/wasm_stats.h"
#include "plugins/asset_test_context.hpp"
#include "plugins/plugin_test_utils.hpp"

// Plugin built from tests/plugin_fixtures/wasm_plugin/wasm_plugin.c with Clang wasm32 + wasm-ld. Only built when both are found.

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using nlohmann::json;

namespace {
    const char* kId = "shard.test.wasm";
#ifdef SHARD_TEST_WASM_PLUGIN_DIR
    const char* kPluginDir = SHARD_TEST_WASM_PLUGIN_DIR;
#else
    const char* kPluginDir = "";
#endif

    class FakeInput : public Core::Platform::IInput {
    public:
        void Init() override {}
        void Tick() override {}
        void Shutdown() override {}
        bool IsKeyDown(Input::Key key) const override { return down.count(key) != 0; }
        bool IsKeyUp(Input::Key key) const override { return down.count(key) == 0; }
        bool WasKeyPressed(Input::Key) const override { return false; }
        bool WasKeyReleased(Input::Key) const override { return false; }
        bool IsMouseDown(Input::MouseButton) const override { return false; }
        bool IsMouseUp(Input::MouseButton) const override { return true; }
        bool WasMousePressed(Input::MouseButton) const override { return false; }
        bool WasMouseReleased(Input::MouseButton) const override { return false; }
        void SetCursorVisibility(Core::Platform::CursorVisibility) const override {}
        void GetCursorPos(double* x, double* y) const override { *x = 0.0; *y = 0.0; }
        void SetCursorPos(double, double) const override {}
        std::set<Input::Key> down;
    };

    class GameContext : public PluginTest::AssetContext {
    public:
        GameContext() { time.Init(1.0f / 60.0f); }
        Time::TimeManager* GetTimeManager() const override { return const_cast<Time::TimeManager*>(&time); }
        Core::Platform::IInput* GetInputManager() const override { return const_cast<FakeInput*>(&input); }
        Time::TimeManager time;
        FakeInput input;
    };

    struct Spin {
        float angle, speed;
        uint64_t note, bulk;
    };

    class WasmSdkPluginTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
#ifndef SHARD_TEST_WASM_PLUGIN_DIR
            GTEST_SKIP() << "no clang + wasm-ld : the sandboxed test plugin was not built";
#else
            PluginManager::Get().ResetForTests();
            config.host = Host::Headless;
            config.registerConsoleCommands = false;
            config.pluginPaths.push_back(kPluginDir);
            Core::SetEngine(&context);
#endif
        }
        void TearDown() override
        {
            world.reset();
            PluginManager::Get().ResetForTests();
            Core::SetEngine(nullptr);
        }

        PluginManager& pm() { return PluginManager::Get(); }
        PluginRecord* Rec() { return pm().Find(kId); }

        uint64_t Call(const char* name, std::vector<uint64_t> args = {})
        {
            std::vector<uint64_t> results;
            std::string error;
            EXPECT_TRUE(CallWasmExport(Rec()->owner, name, args, results, &error)) << error;
            return results.empty() ? 0 : results[0];
        }
        int Stat(int which) { return int(int32_t(uint32_t(Call("wasm_stat", { uint64_t(which) })))); }

        void Start()
        {
            std::string error;
            ASSERT_TRUE(pm().Startup(config, &error)) << error;
            ASSERT_NE(Rec(), nullptr);
            ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
            pm().OnEngineSystemsReady(&context, &scheduler, &events);
            pm().OnEngineInitialized();
            ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
            world = std::make_unique<Worlds::World>("w", Filesystem::Path("w.world"));
        }

        void RunFrame(Core::Ecs::Registry& registry, float dt = 0.5f)
        {
            Core::Ecs::Scheduler::FrameTiming timing;
            timing.deltaTime = dt;
            pm().BeginFrame(++frame);
            scheduler.RunFrame(registry, nullptr, timing);
            pm().EndFrame(frame);
        }

        std::shared_ptr<Objects::Actor> NewActor(const std::string& name)
        {
            auto actor = Core::Object::CreateWithContext<Objects::Actor>(&context, name, &context);
            world->AddActor(actor);
            return actor;
        }
        std::string Type(const char* script) const { return std::string(kId) + "." + script; }

        PluginManagerConfig config;
        GameContext context;
        Core::Ecs::Scheduler scheduler;
        Events::EventDispatcher events;
        std::unique_ptr<Worlds::World> world;
        uint64_t frame = 0;
    };
}

TEST_F(WasmSdkPluginTest, ClangPluginRuns)
{
    Start();
    Core::CVarBase* speed = Core::CVarRegistry::Global().Find(std::string(kId) + ".speed");
    ASSERT_NE(speed, nullptr);
    EXPECT_EQ(speed->ToString(), "3.5");
    EXPECT_EQ(Stat(S_LOAD), 1);
    EXPECT_EQ(Stat(S_INITIALIZE), 1);
    EXPECT_EQ(Stat(S_ENABLE), 1);

    const Core::CommandResult echo = Core::CVarRegistry::Global().Execute("sandbox.echo hello world");
    EXPECT_TRUE(echo.ok) << echo.output;
    EXPECT_EQ(echo.output, "hello");
    EXPECT_EQ(Stat(S_COMMAND_ARGS), 2);
    EXPECT_FALSE(Core::CVarRegistry::Global().Execute("sandbox.echo").ok);
}

TEST_F(WasmSdkPluginTest, PluginChecksHostAnswers)
{
    Start();
    EXPECT_EQ(Stat(S_FAILURES), 0);
    EXPECT_GE(Stat(S_CHECKS), 60);
    EXPECT_EQ(Stat(S_HELLO_BYTES), 2);
    EXPECT_EQ(Stat(S_LISTED), 1);
    EXPECT_GE(Stat(S_REENTRANT), 1);

    EXPECT_EQ(int32_t(uint32_t(Call("wasm_ping", { 9 }))), 9);
    EXPECT_EQ(Stat(S_PING_VALUE), 9);
}

TEST_F(WasmSdkPluginTest, BlobReachesHost)
{
    Start();
    const uint64_t blob = Call("wasm_make_blob");
    ASSERT_NE(blob, 0u);
    std::string bytes;
    ASSERT_TRUE(CAbi::BlobBytes(Rec()->owner, blob, bytes));
    ASSERT_EQ(bytes.size(), 6u);
    EXPECT_EQ(uint8_t(bytes[0]), 10);
    EXPECT_EQ(uint8_t(bytes[5]), 60);
}

TEST_F(WasmSdkPluginTest, SystemRunsOverEntities)
{
    Start();
    const auto spinId = Core::Ecs::ComponentId(Call("wasm_spin_type") - 1);
    Core::Ecs::Registry registry;
    Spin start{ 0.0f, 2.0f, 0, 0 };
    const Core::Ecs::Entity a = registry.Create(), b = registry.Create();
    ASSERT_TRUE(registry.AddRaw(a, spinId, &start));
    start.speed = 4.0f;
    ASSERT_TRUE(registry.AddRaw(b, spinId, &start));
    registry.Create();

    RunFrame(registry, 0.5f);
    EXPECT_EQ(Stat(S_SPIN_RUNS), 1);
    EXPECT_EQ(Stat(S_SPIN_ENTITIES), 2);
    EXPECT_EQ(Stat(S_SPIN_SAME_BLOCK), 1);
    EXPECT_FLOAT_EQ(static_cast<const Spin*>(registry.GetRaw(a, spinId))->angle, 1.0f);
    EXPECT_FLOAT_EQ(static_cast<const Spin*>(registry.GetRaw(b, spinId))->angle, 2.0f);
    RunFrame(registry, 0.5f);
    EXPECT_FLOAT_EQ(static_cast<const Spin*>(registry.GetRaw(a, spinId))->angle, 2.0f);
    EXPECT_EQ(Stat(S_FRAME_BEGIN), 2);
    EXPECT_EQ(Stat(S_FRAME_END), 2);
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(WasmSdkPluginTest, ScriptRunsInModule)
{
    Start();
    auto hero = NewActor("Hero");
    auto mover = hero->AddComponentByName(Type("Mover"));
    ASSERT_NE(mover, nullptr);
    EXPECT_EQ(Stat(S_CONSTRUCTED), 1);
    EXPECT_EQ(Stat(S_CREATED), 1);

    context.time.Advance(0.5f);
    world->Play();
    EXPECT_EQ(Stat(S_PLAYED), 1);
    EXPECT_EQ(mover->Serialize()["label"].get<std::string>(), "hello");
    EXPECT_FLOAT_EQ(mover->Serialize()["speed"].get<float>(), 2.0f);
    ASSERT_EQ(hero->GetComponents().size(), 3u);
    auto counter = hero->GetComponents()[2];
    EXPECT_EQ(counter->Serialize()["count"].get<int>(), 7);
    EXPECT_EQ(Stat(S_COUNTER_SET), 1);

    world->Tick();
    EXPECT_EQ(Stat(S_TICKS), 1);
    EXPECT_FLOAT_EQ(hero->transform->GetPosition().x, 1.0f);
    context.input.down.insert(Input::Key::D);
    world->Tick();
    EXPECT_FLOAT_EQ(hero->transform->GetPosition().x, 3.0f);
    context.input.down.clear();

    mover->Deserialize(json::parse(R"({"active":true,"speed":5.0})"));
    world->Tick();
    EXPECT_FLOAT_EQ(hero->transform->GetPosition().x, 5.5f);

    auto copy = hero->Clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(Stat(S_CONSTRUCTED), 2);
    auto copied = copy->GetComponents()[1];
    EXPECT_FLOAT_EQ(copied->Serialize()["speed"].get<float>(), 5.0f);
    EXPECT_EQ(copied->Serialize()["label"].get<std::string>(), "hello");

    hero->Destroy();
    EXPECT_EQ(Stat(S_DESTROYED), 1);
    EXPECT_EQ(Rec()->errorCount, 0u);
    EXPECT_EQ(Stat(S_FAILURES), 0);
}

TEST_F(WasmSdkPluginTest, WorldToldToModule)
{
    Start();
    world->SetLoaded(true);
    EXPECT_GE(Stat(S_WORLD_LOADED), 0);
}

TEST_F(WasmSdkPluginTest, RunawayModuleStopped)
{
    Start();
    std::vector<uint64_t> results;
    std::string error;
    EXPECT_FALSE(CallWasmExport(Rec()->owner, "wasm_loop", {}, results, &error));
    EXPECT_NE(error.find("fuel"), std::string::npos) << error;

    EXPECT_FALSE(CallWasmExport(Rec()->owner, "wasm_stat", { 0 }, results, &error));
    Core::Ecs::Registry registry;
    for (int i = 0; i < 12; ++i) RunFrame(registry);
    registry.Create();
    EXPECT_NE(Rec()->state, PluginState::Unloaded);
    ASSERT_TRUE(pm().Disable(kId, true, &error) || Rec()->state == PluginState::Failed) << error;
}

TEST_F(WasmSdkPluginTest, NewInstanceTakesSavedState)
{
    config.allowHotReload = true;
    Start();
    EXPECT_EQ(Stat(S_RESTORED), 0);
    std::string error;
    ASSERT_TRUE(pm().Reload(kId, &error)) << error;
    ASSERT_NE(Rec(), nullptr);
    EXPECT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
    EXPECT_EQ(Stat(S_RESTORED), 42);
    EXPECT_EQ(Stat(S_LOAD), 1);
    EXPECT_EQ(Stat(S_FAILURES), 0);
}

TEST_F(WasmSdkPluginTest, CompiledModuleCached)
{
    PluginTest::TempDir cache;
    config.cacheDirectory = cache.Sub("cache");
    const WasmCacheStats before = GetWasmCacheStats();
    {
        Start();
        EXPECT_EQ(Stat(S_FAILURES), 0);
        pm().Shutdown();
    }
    WasmCacheStats after = GetWasmCacheStats();
    EXPECT_EQ(after.misses - before.misses, 1u);
    EXPECT_EQ(after.writes - before.writes, 1u);
    EXPECT_EQ(after.hits - before.hits, 0u);

    PluginManager::Get().ResetForTests();
    config.pluginPaths = { kPluginDir };
    world.reset();
    {
        Start();
        EXPECT_EQ(Stat(S_FAILURES), 0);
        pm().Shutdown();
    }
    after = GetWasmCacheStats();
    EXPECT_EQ(after.hits - before.hits, 1u);
    EXPECT_EQ(after.misses - before.misses, 1u);

    std::filesystem::path compiled;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(cache.Sub("cache")))
        if (entry.path().extension() == ".cwasm") compiled = entry.path();
    ASSERT_FALSE(compiled.empty());
    std::filesystem::resize_file(compiled, std::filesystem::file_size(compiled) / 2);
    PluginManager::Get().ResetForTests();
    config.pluginPaths = { kPluginDir };
    world.reset();
    Start();
    EXPECT_EQ(Stat(S_FAILURES), 0);
    after = GetWasmCacheStats();
    EXPECT_EQ(after.misses - before.misses, 2u);
}

namespace {
    struct SpinRun {
        std::vector<uint32_t> bits;
        uint64_t fuel = 0;
        double ms = 0.0;
    };
}

TEST_F(WasmSdkPluginTest, TwoRunsSameBitsAndFuel)
{
    auto run = [&]() {
        PluginManager::Get().ResetForTests();
        world.reset();
        config.pluginPaths = { kPluginDir };
        Start();
        const auto spinId = Core::Ecs::ComponentId(Call("wasm_spin_type") - 1);
        Core::Ecs::Registry registry;
        std::vector<Core::Ecs::Entity> entities;
        for (int i = 0; i < 500; ++i) {
            Spin s{ 0.1f * float(i), 0.37f + 0.011f * float(i), 0, 0 };
            entities.push_back(registry.Create());
            registry.AddRaw(entities.back(), spinId, &s);
        }
        for (int f = 0; f < 20; ++f) RunFrame(registry, 1.0f / 60.0f);
        SpinRun r;
        for (auto e : entities) {
            const float angle = static_cast<const Spin*>(registry.GetRaw(e, spinId))->angle;
            uint32_t b;
            std::memcpy(&b, &angle, 4);
            r.bits.push_back(b);
        }
        r.fuel = WasmFuelUsed(Rec()->owner);
        return r;
    };
    const SpinRun first = run(), second = run();
    EXPECT_EQ(first.bits, second.bits);
    EXPECT_EQ(first.fuel, second.fuel);
    EXPECT_GT(first.fuel, 0u);
}

// Release only, run with --gtest_also_run_disabled_tests. kWasmFuelPerMs is calibrated on fuel per ms.
TEST_F(WasmSdkPluginTest, DISABLED_MeasuresTheSystemAgainstNative)
{
    Start();
    const auto spinId = Core::Ecs::ComponentId(Call("wasm_spin_type") - 1);
    constexpr int kEntities = 10000, kFrames = 200;
    Core::Ecs::Registry registry;
    std::vector<Core::Ecs::Entity> entities;
    for (int i = 0; i < kEntities; ++i) {
        Spin s{ 0.0f, 0.5f + 0.0001f * float(i), 0, 0 };
        entities.push_back(registry.Create());
        registry.AddRaw(entities.back(), spinId, &s);
    }
    RunFrame(registry, 1.0f / 60.0f);
    const uint64_t fuelBefore = WasmFuelUsed(Rec()->owner);
    const auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < kFrames; ++f) RunFrame(registry, 1.0f / 60.0f);
    const double wasmMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const uint64_t fuel = WasmFuelUsed(Rec()->owner) - fuelBefore;

    std::vector<Spin> data(kEntities);
    for (int i = 0; i < kEntities; ++i) data[size_t(i)] = Spin{ 0.0f, 0.5f + 0.0001f * float(i), 0, 0 };
    const auto n0 = std::chrono::steady_clock::now();
    for (int f = 0; f < kFrames; ++f)
        for (Spin& s : data) s.angle += s.speed * (1.0f / 60.0f);
    const double nativeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - n0).count();
    volatile float sink = data[17].angle;
    (void)sink;

    std::printf("[ bench ] %d entities, %d frames : sandbox %.3f ms/frame (%.0f fuel/frame, %.0f fuel/ms), native loop %.4f ms/frame, ratio %.1f\n",
                kEntities, kFrames, wasmMs / kFrames, double(fuel) / kFrames, double(fuel) / wasmMs, nativeMs / kFrames, wasmMs / nativeMs);
}

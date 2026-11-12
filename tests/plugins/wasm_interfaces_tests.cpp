#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "engine/assets/assetID.hpp"
#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/ecs/registry.hpp"
#include "engine/core/jobs/job_system.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/platform/devices/iinput.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/plugins/wasm/wasm_layout_gen.hpp"
#include "engine/plugins/wasm/wasm_plugin.hpp"
#include "engine/world/time_manager.hpp"
#include "plugin_fixtures/wasm_plugin/wasm_stats.h"
#include "plugins/plugin_test_utils.hpp"
#include "support/test_engine_context.hpp"

// shard.time, shard.assets, shard.jobs and shard.debug through the sandbox, with the real plugin of tests/plugin_fixtures/wasm_plugin (skipped without clang + wasm-ld).

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
namespace fs = std::filesystem;

TEST(WasmInterfaces, SandboxOffersTheNewInterfaces)
{
    std::set<std::string> offered;
    Wasm::Permission assets = Wasm::Permission::None;
    for (const Wasm::OfferedInterface& i : Wasm::kOfferedInterfaces) {
        offered.insert(i.id);
        if (std::string(i.id) == "shard.assets") assets = i.permission;
    }
    for (const char* id : { "shard.time", "shard.assets", "shard.jobs", "shard.debug" }) EXPECT_TRUE(offered.count(id)) << id;
    EXPECT_EQ(assets, Wasm::Permission::FsReadProject);
    for (const char* id : { "shard.render", "shard.net", "shard.reflect", "shard.hooks" }) EXPECT_FALSE(offered.count(id)) << id;
}

#ifdef SHARD_TEST_WASM_PLUGIN_DIR
namespace {
    const char* kId = "shard.test.wasm";

    class FakeInput : public Core::Platform::IInput {
    public:
        void Init() override {}
        void Tick() override {}
        void Shutdown() override {}
        bool IsKeyDown(Input::Key) const override { return false; }
        bool IsKeyUp(Input::Key) const override { return true; }
        bool WasKeyPressed(Input::Key) const override { return false; }
        bool WasKeyReleased(Input::Key) const override { return false; }
        bool IsMouseDown(Input::MouseButton) const override { return false; }
        bool IsMouseUp(Input::MouseButton) const override { return true; }
        bool WasMousePressed(Input::MouseButton) const override { return false; }
        bool WasMouseReleased(Input::MouseButton) const override { return false; }
        void SetCursorVisibility(Core::Platform::CursorVisibility) const override {}
        void GetCursorPos(double* x, double* y) const override { *x = 0.0; *y = 0.0; }
        void SetCursorPos(double, double) const override {}
    };

    class Context : public Shard::Tests::TestEngineContext {
    public:
        explicit Context(const fs::path& project) : m_Root(project)
        {
            time.Init(1.0f / 60.0f);
            fs::create_directories(m_Root / "resources" / "data");
            fs::create_directories(m_Root / "engine");
            files.Init(Filesystem::Path(fs::absolute(m_Root / "resources").generic_string(), true), Filesystem::Path(fs::absolute(m_Root / "engine").generic_string(), true),
                       Filesystem::Path(fs::absolute(m_Root).generic_string(), true));
            ids.InitCounter(501);
        }
        Time::TimeManager* GetTimeManager() const override { return const_cast<Time::TimeManager*>(&time); }
        Core::Platform::IInput* GetInputManager() const override { return const_cast<FakeInput*>(&input); }
        Filesystem::AssetIDManager* GetAssetIDManager() const override { return const_cast<Filesystem::AssetIDManager*>(&ids); }
        Filesystem::FileManager* GetFileManager() const override { return const_cast<Filesystem::FileManager*>(&files); }
        Rendering::IRenderContext* GetRenderContext() const override { return nullptr; }

        void Add(const std::string& name, const std::string& bytes)
        {
            const fs::path file = m_Root / "resources" / name;
            fs::create_directories(file.parent_path());
            std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes;
            files.RegisterAsset(Filesystem::Path(fs::absolute(file).generic_string(), true));
        }

        Time::TimeManager time;
        FakeInput input;
        Filesystem::AssetIDManager ids;
        Filesystem::FileManager files{ ids };

    private:
        fs::path m_Root;
    };

    uint32_t Crc32(const std::string& bytes)
    {
        uint32_t c = ~0u;
        for (unsigned char b : bytes) {
            c ^= b;
            for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        return ~c;
    }
    void Be32(std::string& out, uint32_t v) { for (int s = 24; s >= 0; s -= 8) out.push_back(char((v >> s) & 0xFF)); }
    void Chunk(std::string& out, const char* type, const std::string& data)
    {
        Be32(out, uint32_t(data.size()));
        const std::string body = std::string(type) + data;
        out += body;
        Be32(out, Crc32(body));
    }

    std::string Png()
    {
        const unsigned char pixels[16] = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 10, 20, 30, 40 };
        std::string raw;
        for (int y = 0; y < 2; ++y) {
            raw.push_back(0);
            raw.append(reinterpret_cast<const char*>(pixels) + y * 8, 8);
        }
        uint32_t a = 1, b = 0;
        for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
        std::string z = { char(0x78), char(0x01), char(0x01) };
        const uint16_t n = uint16_t(raw.size());
        z.push_back(char(n & 0xFF)); z.push_back(char(n >> 8)); z.push_back(char(~n & 0xFF)); z.push_back(char((~n >> 8) & 0xFF));
        z += raw;
        Be32(z, (b << 16) | a);
        std::string ihdr;
        Be32(ihdr, 2); Be32(ihdr, 2);
        ihdr += { 8, 6, 0, 0, 0 };
        std::string png = { char(0x89), 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        Chunk(png, "IHDR", ihdr);
        Chunk(png, "IDAT", z);
        Chunk(png, "IEND", "");
        return png;
    }

    class WasmInterfacesTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            context = std::make_unique<Context>(temp.Sub("project"));
            context->Add("data/hello.txt", "hello assets");
            context->Add("data/pic.png", Png());
            PluginManager::Get().ResetForTests();
            config.host = Host::Headless;
            config.registerConsoleCommands = false;
            config.pluginPaths.push_back(SHARD_TEST_WASM_PLUGIN_DIR);
            physics.Init(glm::vec3(0.0f, -9.81f, 0.0f));
            context->physicsManager = &physics;
            Core::SetEngine(context.get());
        }
        void TearDown() override
        {
            PluginManager::Get().ResetForTests();
            Core::SetEngine(nullptr);
            physics.Shutdown();
            context->physicsManager = nullptr;
            jobs.reset();
        }

        void UsePermissions(const std::vector<std::string>& permissions)
        {
            const fs::path copy = fs::path(temp.Sub("plugin"));
            fs::create_directories(copy / "content");
            fs::copy_file(fs::path(SHARD_TEST_WASM_PLUGIN_DIR) / "plugin.wasm", copy / "plugin.wasm", fs::copy_options::overwrite_existing);
            fs::copy_file(fs::path(SHARD_TEST_WASM_PLUGIN_DIR) / "content" / "hello.txt", copy / "content" / "hello.txt", fs::copy_options::overwrite_existing);
            std::ifstream in(fs::path(SHARD_TEST_WASM_PLUGIN_DIR) / "shard-plugin.json");
            nlohmann::json manifest = nlohmann::json::parse(in);
            manifest["permissions"] = permissions;
            std::ofstream(copy / "shard-plugin.json") << manifest.dump(2);
            config.pluginPaths.assign(1, copy.generic_string());
        }
        void Workers(uint32_t count)
        {
            Core::JobSystemDesc desc;
            desc.workerCount = count;
            jobs = std::make_unique<Core::JobSystem>(desc);
            context->jobSystem = jobs.get();
        }

        PluginManager& pm() { return PluginManager::Get(); }
        PluginRecord* Rec() { return pm().Find(kId); }

        void Start()
        {
            std::string error;
            ASSERT_TRUE(pm().Startup(config, &error)) << error;
            ASSERT_NE(Rec(), nullptr);
            ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
            pm().OnEngineSystemsReady(context.get(), &scheduler, &events);
            pm().OnEngineInitialized();
            ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
        }
        void Frame()
        {
            pm().BeginFrame(++frame);
            pm().EndFrame(frame);
        }
        void Steps(int count)
        {
            for (int i = 0; i < count; ++i) physics.StepSimulation(1.0f / 60.0f, false);
        }

        bool TryCall(const char* name, std::vector<uint64_t> args, uint64_t& result, std::string* error = nullptr)
        {
            std::vector<uint64_t> results;
            std::string why;
            const bool ok = CallWasmExport(Rec()->owner, name, args, results, &why);
            if (error) *error = why;
            result = results.empty() ? 0 : results[0];
            return ok;
        }
        uint64_t Call(const char* name, std::vector<uint64_t> args = {})
        {
            uint64_t result = 0;
            std::string error;
            EXPECT_TRUE(TryCall(name, args, result, &error)) << name << " : " << error;
            return result;
        }
        int R(const char* name, std::vector<uint64_t> args = {}) { return int(int32_t(uint32_t(Call(name, args)))); }
        int Stat(int which) { return R("wasm_stat", { uint64_t(which) }); }
        std::vector<int> Log()
        {
            std::vector<int> log;
            const int count = R("wasm_jobs_log_count");
            for (int i = 0; i < count; ++i) log.push_back(R("wasm_jobs_log", { uint64_t(i) }));
            return log;
        }

        PluginTest::TempDir temp;
        std::unique_ptr<Context> context;
        std::unique_ptr<Core::JobSystem> jobs;
        Physics::PhysicsManager physics;
        PluginManagerConfig config;
        Core::Ecs::Scheduler scheduler;
        Events::EventDispatcher events;
        uint64_t frame = 0;
    };

    ShardTimeInfo NativeTime()
    {
        ShardTimeInfo info{};
        info.structSize = sizeof(info);
        static_cast<const ShardTimeApi*>(CAbi::TimeApi())->get(&info);
        return info;
    }
}

// ---- shard.time ----

TEST_F(WasmInterfacesTest, TimeIsTheSimulationsNeverTheMachines)
{
    Start();
    Steps(30);
    EXPECT_EQ(R("wasm_time_probe"), SHARD_OK);
    EXPECT_EQ(Call("wasm_time_fixed_step"), 30u);
    EXPECT_EQ(Call("wasm_time_sim_us"), 500000u);
    EXPECT_EQ(R("wasm_time_fixed_dt_us"), 16667);
    EXPECT_EQ(R("wasm_time_scale_milli"), 1000);
    EXPECT_EQ(R("wasm_time_real_is_sim"), 1);

    const ShardTimeInfo native = NativeTime();
    EXPECT_GT(native.realTime, 0.0);
    EXPECT_NE(native.realTime, native.simTime);
    EXPECT_EQ(Stat(S_FAILURES), 0);
}

TEST_F(WasmInterfacesTest, TimeSettersNeedAPermission)
{
    Start();
    EXPECT_EQ(R("wasm_time_probe"), SHARD_OK);
    EXPECT_EQ(R("wasm_time_set_scale", { 500 }), SHARD_OK);
    EXPECT_EQ(R("wasm_time_set_scale", { uint64_t(uint32_t(-1000)) }), SHARD_E_INVALID_ARG);
    EXPECT_EQ(R("wasm_time_set_fixed_us", { 8000 }), SHARD_OK);
    EXPECT_EQ(R("wasm_time_probe"), SHARD_OK);
    EXPECT_EQ(R("wasm_time_scale_milli"), 500);
    EXPECT_EQ(R("wasm_time_fixed_dt_us"), 8000);
}

TEST_F(WasmInterfacesTest, TimeSettersRefusedWithoutThePermission)
{
    UsePermissions({ "fs.read:content", "input" });
    Start();
    EXPECT_EQ(R("wasm_time_probe"), SHARD_OK);
    EXPECT_EQ(R("wasm_time_set_scale", { 0 }), SHARD_E_UNSUPPORTED);
    EXPECT_EQ(R("wasm_time_set_fixed_us", { 1000 }), SHARD_E_UNSUPPORTED);
    EXPECT_EQ(R("wasm_time_probe"), SHARD_OK);
    EXPECT_EQ(R("wasm_time_scale_milli"), 1000);
    EXPECT_EQ(R("wasm_time_fixed_dt_us"), 16667);
}

TEST_F(WasmInterfacesTest, TimeStateHashIsTheSimulationsAndRepeats)
{
    Start();
    ASSERT_EQ(R("wasm_time_probe"), SHARD_OK);
    Steps(3);
    EXPECT_EQ(R("wasm_time_hash"), SHARD_OK);
    const uint64_t first = Call("wasm_time_hash_total");
    EXPECT_EQ(R("wasm_time_hash"), SHARD_OK);
    EXPECT_EQ(Call("wasm_time_hash_total"), first);
    Steps(1);
    EXPECT_EQ(R("wasm_time_hash"), SHARD_OK);
    EXPECT_NE(Call("wasm_time_hash_total"), first);
}

// ---- shard.jobs ----

TEST_F(WasmInterfacesTest, JobsRunInOrderWhenWaitedFor)
{
    Workers(8);
    Start();
    ASSERT_EQ(R("wasm_jobs_open"), SHARD_OK);
    EXPECT_EQ(R("wasm_jobs_workers"), 1);
    ASSERT_EQ(R("wasm_jobs_submit"), SHARD_OK);
    EXPECT_EQ(Stat(S_JOB_RUNS), 0);

    EXPECT_EQ(R("wasm_jobs_wait_b"), SHARD_OK);
    EXPECT_EQ(Log(), (std::vector<int>{ 1, 2 }));
    EXPECT_EQ(R("wasm_jobs_wait_b"), SHARD_E_STALE_HANDLE);
    EXPECT_EQ(R("wasm_jobs_release_b"), SHARD_E_STALE_HANDLE);
    EXPECT_EQ(R("wasm_jobs_is_done_a"), SHARD_TRUE);
    EXPECT_EQ(R("wasm_jobs_wait_a"), SHARD_OK);

    Frame();
    EXPECT_EQ(Log(), (std::vector<int>{ 1, 2, 3 }));
    EXPECT_EQ(Stat(S_JOB_RUNS), 3);
    EXPECT_EQ(Stat(S_JOB_WORKER_BAD), 0);
    EXPECT_EQ(Stat(S_FAILURES), 0);
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(WasmInterfacesTest, JobsRunAtTheEndOfTheFrame)
{
    Start();
    ASSERT_EQ(R("wasm_jobs_open"), SHARD_OK);
    ASSERT_EQ(R("wasm_jobs_submit"), SHARD_OK);
    EXPECT_EQ(Stat(S_JOB_RUNS), 0);
    pm().BeginFrame(++frame);
    EXPECT_EQ(Stat(S_JOB_RUNS), 0);
    pm().EndFrame(frame);
    EXPECT_EQ(Log(), (std::vector<int>{ 1, 2, 3 }));
    EXPECT_EQ(R("wasm_jobs_is_done_a"), SHARD_TRUE);
    EXPECT_EQ(R("wasm_jobs_release_b"), SHARD_OK);
    EXPECT_EQ(R("wasm_jobs_release_b"), SHARD_E_STALE_HANDLE);
    EXPECT_EQ(R("wasm_jobs_wait_a"), SHARD_OK);
}

TEST_F(WasmInterfacesTest, AskingIfADoneJobIsDoneGivesItTheTime)
{
    Start();
    ASSERT_EQ(R("wasm_jobs_open"), SHARD_OK);
    ASSERT_EQ(R("wasm_jobs_submit"), SHARD_OK);
    EXPECT_EQ(R("wasm_jobs_is_done_a"), SHARD_TRUE);
    EXPECT_EQ(Log(), (std::vector<int>{ 1 }));
    EXPECT_EQ(R("wasm_jobs_release_b"), SHARD_OK);
    EXPECT_EQ(R("wasm_jobs_wait_b"), SHARD_E_STALE_HANDLE);
    Frame();
    EXPECT_EQ(Log(), (std::vector<int>{ 1, 2, 3 }));
}

TEST_F(WasmInterfacesTest, ParallelForRunsTheSlicesInOrderBeforeItReturns)
{
    Workers(4);
    Start();
    ASSERT_EQ(R("wasm_jobs_open"), SHARD_OK);
    EXPECT_EQ(R("wasm_jobs_parallel_for", { 10, 3 }), SHARD_OK);
    EXPECT_EQ(Stat(S_RANGE_CALLS), 4);
    EXPECT_EQ(R("wasm_jobs_slot_sum"), 55);
    EXPECT_EQ(R("wasm_jobs_parallel_for", { 100, 0 }), SHARD_OK);
    EXPECT_EQ(Stat(S_RANGE_CALLS), 1);
    EXPECT_EQ(R("wasm_jobs_slot_sum"), 5050);
    EXPECT_EQ(R("wasm_jobs_parallel_for", { 100, 1 }), SHARD_OK);
    EXPECT_EQ(Stat(S_RANGE_CALLS), 100);
    EXPECT_EQ(R("wasm_jobs_parallel_for", { 0, 1 }), SHARD_OK);
    EXPECT_EQ(Stat(S_RANGE_CALLS), 0);
    EXPECT_EQ(Stat(S_JOB_WORKER_BAD), 0);
    EXPECT_EQ(Stat(S_FAILURES), 0);
}

TEST_F(WasmInterfacesTest, ParallelForAsyncIsWaitedFor)
{
    Start();
    ASSERT_EQ(R("wasm_jobs_open"), SHARD_OK);
    EXPECT_EQ(R("wasm_jobs_parallel_async_no_out"), SHARD_E_INVALID_ARG);
    EXPECT_EQ(R("wasm_jobs_parallel_async", { 100, 10 }), SHARD_OK);
    EXPECT_EQ(Stat(S_RANGE_CALLS), 0);
    EXPECT_EQ(R("wasm_jobs_wait_c"), SHARD_OK);
    EXPECT_EQ(Stat(S_RANGE_CALLS), 10);
    EXPECT_EQ(R("wasm_jobs_slot_sum"), 5050);
    EXPECT_EQ(R("wasm_jobs_wait_c"), SHARD_E_STALE_HANDLE);
    EXPECT_EQ(R("wasm_jobs_parallel_async", { 0, 10 }), SHARD_OK);
    EXPECT_EQ(R("wasm_jobs_wait_c"), SHARD_OK);
}

TEST_F(WasmInterfacesTest, RunWithoutAFunctionIsRefused)
{
    Start();
    ASSERT_EQ(R("wasm_jobs_open"), SHARD_OK);
    EXPECT_EQ(R("wasm_jobs_run_null"), SHARD_E_INVALID_ARG);
}

TEST_F(WasmInterfacesTest, AJobCannotWaitForItself)
{
    Start();
    ASSERT_EQ(R("wasm_jobs_open"), SHARD_OK);
    ASSERT_EQ(R("wasm_jobs_submit_self"), SHARD_OK);
    Frame();
    EXPECT_EQ(Stat(S_JOB_RUNS), 1);
    EXPECT_EQ(Stat(S_JOB_SELF_WAIT), SHARD_E_INVALID_ARG);
    EXPECT_EQ(R("wasm_jobs_wait_self"), SHARD_OK);
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(WasmInterfacesTest, AJobHandsItsResultToTheMainThread)
{
    Start();
    ASSERT_EQ(R("wasm_jobs_open"), SHARD_OK);
    ASSERT_EQ(R("wasm_jobs_submit_post", { 7 }), SHARD_OK);
    Frame();
    EXPECT_EQ(Stat(S_JOB_RUNS), 1);
    EXPECT_EQ(Stat(S_POST_RUNS), 0);
    Frame();
    EXPECT_EQ(Stat(S_POST_RUNS), 1);
    EXPECT_EQ(Stat(S_POST_VALUE), 7);

    EXPECT_EQ(R("wasm_jobs_post", { 5 }), SHARD_OK);
    EXPECT_EQ(Stat(S_POST_RUNS), 1);
    Frame();
    EXPECT_EQ(Stat(S_POST_RUNS), 2);
    EXPECT_EQ(Stat(S_POST_VALUE), 5);
}

TEST_F(WasmInterfacesTest, RunawayJobStopsThePluginAndNotTheEngine)
{
    Start();
    ASSERT_EQ(R("wasm_jobs_open"), SHARD_OK);
    ASSERT_EQ(R("wasm_jobs_submit_spin"), SHARD_OK);
    Frame();
    uint64_t ignored = 0;
    std::string error;
    EXPECT_FALSE(TryCall("wasm_stat", { 0 }, ignored, &error));
    Frame();
    EXPECT_NE(Rec()->state, PluginState::Unloaded);
}

TEST_F(WasmInterfacesTest, DisablingRunsTheQueuedJobs)
{
    Start();
    ASSERT_EQ(R("wasm_jobs_open"), SHARD_OK);
    ASSERT_EQ(R("wasm_jobs_submit"), SHARD_OK);
    EXPECT_EQ(Stat(S_JOB_RUNS), 0);
    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_EQ(Stat(S_JOB_RUNS), 3);
}

TEST_F(WasmInterfacesTest, JobsDoNotDependOnTheEngineWorkers)
{
    auto run = [&](uint32_t workers) {
        PluginManager::Get().ResetForTests();
        config.pluginPaths = { SHARD_TEST_WASM_PLUGIN_DIR };
        Workers(workers);
        Start();
        EXPECT_EQ(R("wasm_jobs_open"), SHARD_OK);
        EXPECT_EQ(R("wasm_jobs_workers"), 1);
        EXPECT_EQ(R("wasm_jobs_submit"), SHARD_OK);
        EXPECT_EQ(R("wasm_jobs_parallel_for", { 100, 7 }), SHARD_OK);
        Frame();
        std::vector<int> log = Log();
        log.push_back(Stat(S_RANGE_CALLS));
        log.push_back(R("wasm_jobs_slot_sum"));
        return log;
    };
    const std::vector<int> one = run(1), many = run(8);
    EXPECT_EQ(one, many);
    EXPECT_EQ(one.size(), 5u);
}

// ---- shard.assets ----

TEST_F(WasmInterfacesTest, AssetsAreReadFromTheProject)
{
    Start();
    ASSERT_EQ(R("wasm_assets_open"), SHARD_OK);
    EXPECT_EQ(R("wasm_assets_probe"), 0);
    EXPECT_EQ(Stat(S_FAILURES), 0);
    const int expected[16] = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 10, 20, 30, 40 };
    for (int i = 0; i < 16; ++i) EXPECT_EQ(R("wasm_assets_pixel", { uint64_t(i) }), expected[i]) << i;
    EXPECT_EQ(R("wasm_assets_pixel", { 16 }), -1);
}

TEST_F(WasmInterfacesTest, AssetsWriteStaysInTheProject)
{
    Start();
    ASSERT_EQ(R("wasm_assets_open"), SHARD_OK);
    EXPECT_EQ(R("wasm_assets_write"), SHARD_OK);
    EXPECT_EQ(temp.Read("project/resources/out/baked.bin"), "xyz");
    EXPECT_TRUE(fs::exists(temp.Sub("project/resources/out/baked.bin")));
    EXPECT_EQ(R("wasm_assets_write_no_id"), SHARD_OK);
    EXPECT_EQ(temp.Read("project/resources/out/noid.bin"), "q");

    EXPECT_EQ(R("wasm_assets_write_escape"), SHARD_E_INVALID_ARG);
    EXPECT_EQ(R("wasm_assets_write_absolute"), SHARD_E_INVALID_ARG);
    EXPECT_FALSE(fs::exists(temp.Sub("project/escaped.bin")));
    EXPECT_FALSE(fs::exists("C:/escaped.bin"));
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(WasmInterfacesTest, AssetsAreNotOfferedWithoutTheReadPermission)
{
    UsePermissions({ "fs.read:content", "input", "fs.write:project" });
    Start();
    EXPECT_EQ(R("wasm_assets_open"), -100);
}

TEST_F(WasmInterfacesTest, AssetsWriteNeedsItsOwnPermission)
{
    UsePermissions({ "fs.read:content", "input", "fs.read:project" });
    Start();
    ASSERT_EQ(R("wasm_assets_open"), SHARD_OK);
    EXPECT_EQ(R("wasm_assets_probe"), 0);
    EXPECT_EQ(R("wasm_assets_write"), -1);
    EXPECT_FALSE(fs::exists(temp.Sub("project/resources/out/baked.bin")));
    EXPECT_EQ(R("wasm_assets_write_no_id"), SHARD_E_UNSUPPORTED);
}

TEST_F(WasmInterfacesTest, AMeshArrayOutsideTheMemoryStopsTheModule)
{
    Start();
    ASSERT_EQ(R("wasm_assets_open"), SHARD_OK);
    uint64_t ignored = 0;
    std::string error;
    EXPECT_FALSE(TryCall("wasm_assets_mesh_oob", {}, ignored, &error));
    EXPECT_NE(error.find("out of the memory"), std::string::npos) << error;
}

// ---- shard.debug ----

TEST_F(WasmInterfacesTest, DebugDrawingIsAccepted)
{
    Start();
    ASSERT_EQ(R("wasm_debug_open"), SHARD_OK);
    EXPECT_EQ(R("wasm_debug_probe"), SHARD_OK);
    EXPECT_EQ(Stat(S_FAILURES), 0);
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(WasmInterfacesTest, DebugPointsOutsideTheMemoryStopTheModule)
{
    Start();
    ASSERT_EQ(R("wasm_debug_open"), SHARD_OK);
    uint64_t ignored = 0;
    std::string error;
    EXPECT_FALSE(TryCall("wasm_debug_oob", {}, ignored, &error));
    EXPECT_NE(error.find("out of the memory"), std::string::npos) << error;
}
#endif

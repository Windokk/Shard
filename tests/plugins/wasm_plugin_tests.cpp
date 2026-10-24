#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "engine/core/config/cvar.hpp"
#include "engine/core/ecs/registry.hpp"
#include "engine/plugins/manager.hpp"
#include "plugins/plugin_test_utils.hpp"
#include "plugins/wasm_test_module.hpp"

// Sandboxed plugins end to end (docs/design-notes/wasm-sandbox.fr.md, W3 / W4) : a tier 2 plugin folder whose binary is a WebAssembly module
// written byte by byte, loaded by the real PluginManager.

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using namespace WasmTest;
using nlohmann::json;
using PluginTest::TempDir;

namespace {
    const char* kId = "com.acme.sandboxed";

    std::vector<uint8_t> Store(uint32_t offset, std::vector<uint8_t> address, std::vector<uint8_t> value)
    {
        std::vector<uint8_t> o = address + value;
        o.push_back(0x36);
        o.push_back(2);
        WriteU32(o, offset);
        return o;
    }
    std::string U32Bytes(uint32_t v) { std::string s(4, '\0'); std::memcpy(&s[0], &v, 4); return s; }
    std::string U64Bytes(uint64_t v) { std::string s(8, '\0'); std::memcpy(&s[0], &v, 8); return s; }

    enum class Behaviour { Fine, SpinOnEnable, ReadOutOfBoundsOnEnable };

    // shard_plugin_entry fills the wasm32 ShardPluginInfo ; on_load logs, on_enable counts its calls at address 256 (or misbehaves)
    std::vector<uint8_t> PluginModule(Behaviour behaviour)
    {
        Builder b;
        const uint32_t entry = b.Type({ I32, I32 }, { I32 });
        const uint32_t lifecycle = b.Type({ I32, I64 }, { I32 });
        const uint32_t log = b.Type({ I64, I32, I32 }, {});
        b.Import("shard", "log", log);                                              // function 0
        b.memoryMin = 1;
        b.tableSize = 4;

        // 0 : the message, 64 : its ShardStr, 128 : the callbacks (structSize, on_load 1, on_initialize 0, on_enable 2, the rest 0)
        b.data.push_back({ 0, "hello from the sandbox" });
        b.data.push_back({ 64, U32Bytes(0) + U32Bytes(0) + U64Bytes(22) });
        std::string callbacks = U32Bytes(4 + 4 * 9) + U32Bytes(1) + U32Bytes(0) + U32Bytes(2);
        for (int i = 0; i < 6; ++i) callbacks += U32Bytes(0);
        b.data.push_back({ 128, callbacks });

        const uint32_t entryFn = b.Function(entry, Store(0, Get(1), I32Const(40)) + Store(4, Get(1), I32Const(1)) + Store(8, Get(1), I32Const(0)) +
                                                   Store(32, Get(1), I32Const(7)) + Store(36, Get(1), I32Const(128)) + I32Const(0) + Op({ 0x0B }),
                                            {}, "shard_plugin_entry");
        (void)entryFn;
        // on_load(user, ctx) : log, and refuse unless `user` came back as given
        const uint32_t onLoad = b.Function(lifecycle, Get(1) + I32Const(0) + I32Const(64) + Call(0) + Get(0) + I32Const(7) + Op({ 0x47, 0x0B }));
        std::vector<uint8_t> enable;
        if (behaviour == Behaviour::SpinOnEnable) enable = Op({ 0x03, 0x40, 0x0C, 0, 0x0B }) + I32Const(0) + Op({ 0x0B });
        else if (behaviour == Behaviour::ReadOutOfBoundsOnEnable) enable = I32Const(0x7FFFFFF0) + Op({ 0x28, 2, 0, 0x0B });
        else enable = Store(0, I32Const(256), I32Const(256) + Op({ 0x28, 2, 0 }) + I32Const(1) + Op({ 0x6A })) + I32Const(0) + Op({ 0x0B });
        const uint32_t onEnable = b.Function(lifecycle, enable);
        b.elements.push_back({ 1, { onLoad, onEnable } });
        return b.Build();
    }

    class WasmPluginTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            PluginManager::Get().ResetForTests();
            config.host = Host::Headless;
            config.registerConsoleCommands = false;
        }
        void TearDown() override { PluginManager::Get().ResetForTests(); }

        // A plugin folder : the manifest (tier 2, abi wasm1) and plugin.wasm
        void Folder(const std::vector<uint8_t>& wasm, json patch = json::object())
        {
            json manifest = PluginTest::Manifest(kId);
            manifest["linkage"] = "dynamic";
            manifest["hosts"] = json::array({ "engine" });
            manifest["tier"] = 2;
            manifest["abi"] = "wasm1";
            manifest["binaries"] = { { "wasm", "plugin.wasm" } };
            manifest.merge_patch(patch);
            dir.Write("p/shard-plugin.json", manifest.dump());
            dir.Write("p/plugin.wasm", std::string(wasm.begin(), wasm.end()));
            config.pluginPaths = { dir.Sub("p") };
        }

        PluginManager& pm() { return PluginManager::Get(); }
        PluginRecord* Rec() { return pm().Find(kId); }

        TempDir dir;
        PluginManagerConfig config;
    };
}

TEST_F(WasmPluginTest, TheManifestNamesOneBinaryForEveryPlatform)
{
    json manifest = PluginTest::Manifest(kId);
    manifest["abi"] = "wasm1";
    manifest["tier"] = 2;
    manifest["binaries"] = { { "wasm", "plugin.wasm" } };
    auto parsed = ParseManifestText(manifest.dump());
    ASSERT_TRUE(parsed) << parsed.GetError().message;
    EXPECT_EQ(parsed.Value().entry, "shard_plugin_entry");
    EXPECT_EQ(parsed.Value().BinaryForThisPlatform(), "plugin.wasm");
}

TEST_F(WasmPluginTest, ASandboxedPluginLoadsWithoutSignatureOrConsent)
{
    Folder(PluginModule(Behaviour::Fine));
    std::string error;
    ASSERT_TRUE(pm().Startup(config, &error)) << error;
    ASSERT_NE(Rec(), nullptr);
    ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;     // on_load checked `user` and returned 0
    EXPECT_EQ(Rec()->desc, nullptr);
    EXPECT_FALSE(Rec()->library.IsOpen());                                  // nothing was mapped into the process

    pm().OnEngineInitialized();
    EXPECT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    ASSERT_TRUE(pm().Enable(kId, &error)) << error;
    EXPECT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
    pm().Shutdown();
}

TEST_F(WasmPluginTest, ABudgetExhaustedInACallbackFailsThePluginNotTheEngine)
{
    Folder(PluginModule(Behaviour::SpinOnEnable), { { "budgets", { { "frameMs", 0.01 } } } });
    std::string error;
    ASSERT_TRUE(pm().Startup(config, &error)) << error;
    ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
    pm().OnEngineInitialized();
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    pm().Shutdown();
}

TEST_F(WasmPluginTest, AnAccessOutOfItsMemoryFailsThePluginNotTheEngine)
{
    Folder(PluginModule(Behaviour::ReadOutOfBoundsOnEnable));
    std::string error;
    ASSERT_TRUE(pm().Startup(config, &error)) << error;
    pm().OnEngineInitialized();
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    pm().Shutdown();
}

TEST_F(WasmPluginTest, ATier2PluginMustBeAWasmModule)
{
    Folder(PluginModule(Behaviour::Fine), { { "abi", "c1" }, { "binaries", { { CurrentPlatformKey(), "plugin.wasm" } } } });
    pm().Startup(config);
    ASSERT_NE(Rec(), nullptr);
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    EXPECT_NE(Rec()->error.find("wasm1"), std::string::npos) << Rec()->error;
}

TEST_F(WasmPluginTest, AMalformedModuleIsRefusedWithTheReason)
{
    std::vector<uint8_t> broken = PluginModule(Behaviour::Fine);
    broken.resize(broken.size() / 2);
    Folder(broken);
    pm().Startup(config);
    ASSERT_NE(Rec(), nullptr);
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    EXPECT_NE(Rec()->error.find("sandbox"), std::string::npos) << Rec()->error;
}

TEST_F(WasmPluginTest, AnImportTheHostDoesNotGiveIsRefused)
{
    Builder b;
    const uint32_t t = b.Type({}, {});
    b.Import("wasi_snapshot_preview1", "fd_write", t);
    b.memoryMin = 1;
    b.Function(b.Type({ I32, I32 }, { I32 }), I32Const(0) + Op({ 0x0B }), {}, "shard_plugin_entry");
    Folder(b.Build());
    pm().Startup(config);
    ASSERT_NE(Rec(), nullptr);
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    EXPECT_NE(Rec()->error.find("fd_write"), std::string::npos) << Rec()->error;
}

namespace {
    std::vector<uint8_t> F32Const(float v) { std::vector<uint8_t> o = { 0x43, 0, 0, 0, 0 }; std::memcpy(&o[1], &v, 4); return o; }

    // on_load creates the float cvar "speed" (2.5, in [0, 10]) and keeps its handle at 512 ; on_enable reads it and adds 1
    std::vector<uint8_t> CVarModule()
    {
        Builder b;
        const uint32_t entry = b.Type({ I32, I32 }, { I32 });
        const uint32_t lifecycle = b.Type({ I32, I64 }, { I32 });
        b.Import("shard", "cvars.create_float", b.Type({ I64, I32, F32, I32, I32, F32, F32, I32 }, { I32 }));     // 0
        b.Import("shard", "cvars.get_float", b.Type({ I64, I64, I32 }, { I32 }));                               // 1
        b.Import("shard", "cvars.set_float", b.Type({ I64, I64, F32 }, { I32 }));                               // 2
        b.memoryMin = 1;
        b.tableSize = 4;
        b.data.push_back({ 300, "speed" });
        b.data.push_back({ 320, "how fast" });
        b.data.push_back({ 336, U32Bytes(300) + U32Bytes(0) + U64Bytes(5) + U32Bytes(320) + U32Bytes(0) + U64Bytes(8) });
        std::string callbacks = U32Bytes(4 + 4 * 9) + U32Bytes(1) + U32Bytes(0) + U32Bytes(2);
        for (int i = 0; i < 6; ++i) callbacks += U32Bytes(0);
        b.data.push_back({ 128, callbacks });
        b.Function(entry, Store(0, Get(1), I32Const(40)) + Store(4, Get(1), I32Const(1)) + Store(36, Get(1), I32Const(128)) + I32Const(0) + Op({ 0x0B }),
                   {}, "shard_plugin_entry");
        const uint32_t onLoad = b.Function(lifecycle, Get(1) + I32Const(336) + F32Const(2.5f) + I32Const(352) + I32Const(0) + F32Const(0.f) + F32Const(10.f) +
                                                      I32Const(512) + Call(0) + Op({ 0x0B }));
        const uint32_t onEnable = b.Function(lifecycle, Get(1) + I32Const(512) + Op({ 0x29, 3, 0 }) + I32Const(520) + Call(1) + Op({ 0x1A }) +
                                                        Get(1) + I32Const(512) + Op({ 0x29, 3, 0 }) + I32Const(520) + Op({ 0x2A, 2, 0 }) + F32Const(1.f) +
                                                        Op({ 0x92 }) + Call(2) + Op({ 0x0B }));
        b.elements.push_back({ 1, { onLoad, onEnable } });
        return b.Build();
    }
}

TEST_F(WasmPluginTest, CVarsMadeInTheSandboxAreTheHostsAndScopedToThePlugin)
{
    Folder(CVarModule());
    std::string error;
    ASSERT_TRUE(pm().Startup(config, &error)) << error;
    ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
    Core::CVarBase* speed = Core::CVarRegistry::Global().Find(std::string(kId) + ".speed");
    ASSERT_NE(speed, nullptr);
    EXPECT_EQ(speed->ToString(), "2.5");
    pm().OnEngineInitialized();
    ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
    EXPECT_EQ(speed->ToString(), "3.5");

    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_EQ(Core::CVarRegistry::Global().Find(std::string(kId) + ".speed"), nullptr);       // gone with the plugin, like a native one's
    pm().Shutdown();
}

namespace {
    // on_load adds the command "sandbox.echo" ; the command answers with its first word, or fails without one
    std::vector<uint8_t> CommandModule()
    {
        Builder b;
        const uint32_t entry = b.Type({ I32, I32 }, { I32 });
        const uint32_t lifecycle = b.Type({ I32, I64 }, { I32 });
        const uint32_t command = b.Type({ I32, I64, I32, I32, I32 }, { I32 });
        b.Import("shard", "commands.add", b.Type({ I64, I32, I32, I32, I32 }, { I32 }));                // 0
        b.memoryMin = 1;
        b.tableSize = 4;
        b.data.push_back({ 300, "sandbox.echo" });
        b.data.push_back({ 320, "repeats a word" });
        b.data.push_back({ 336, U32Bytes(300) + U32Bytes(0) + U64Bytes(12) + U32Bytes(320) + U32Bytes(0) + U64Bytes(14) });
        std::string callbacks = U32Bytes(4 + 4 * 9) + U32Bytes(1);
        for (int i = 0; i < 8; ++i) callbacks += U32Bytes(0);
        b.data.push_back({ 128, callbacks });
        b.Function(entry, Store(0, Get(1), I32Const(40)) + Store(4, Get(1), I32Const(1)) + Store(36, Get(1), I32Const(128)) + I32Const(0) + Op({ 0x0B }),
                   {}, "shard_plugin_entry");
        const uint32_t onLoad = b.Function(lifecycle, Get(1) + I32Const(336) + I32Const(352) + I32Const(2) + I32Const(0) + Call(0) + Op({ 0x0B }));
        // echo(user, ctx, args, argCount, output) : argCount == 0 ? -1 : (*output = args[0], 0)
        const uint32_t echo = b.Function(command, Get(3) + Op({ 0x45, 0x04, 0x40 }) + I32Const(-1) + Op({ 0x0F, 0x0B }) +
                                                  Get(4) + Get(2) + Op({ 0x29, 3, 0, 0x37, 3, 0 }) + Get(4) + Get(2) + Op({ 0x29, 3, 8, 0x37, 3, 8 }) +
                                                  I32Const(0) + Op({ 0x0B }));
        b.elements.push_back({ 1, { onLoad, echo } });
        return b.Build();
    }
}

TEST_F(WasmPluginTest, ACommandWrittenInTheSandboxRunsFromTheConsole)
{
    Folder(CommandModule());
    std::string error;
    ASSERT_TRUE(pm().Startup(config, &error)) << error;
    ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
    auto& console = Core::CVarRegistry::Global();
    const Core::CommandResult ok = console.Execute("sandbox.echo hello world");
    EXPECT_TRUE(ok.ok) << ok.output;
    EXPECT_EQ(ok.output, "hello");                                    // the words went in as wasm32 ShardStr, the text came back out
    EXPECT_FALSE(console.Execute("sandbox.echo").ok);

    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_FALSE(console.Execute("sandbox.echo hello").ok);           // removed with the plugin
    pm().Shutdown();
}

namespace {
    // on_frame_begin (and on_frame_end if asked) each count down from 1500 : 4 or 5 units of fuel an iteration, 6 000 to 7 500 apiece
    std::vector<uint8_t> FrameModule(bool alsoAtTheEnd)
    {
        Builder b;
        const uint32_t entry = b.Type({ I32, I32 }, { I32 });
        const uint32_t frame = b.Type({ I32, I64, I64 }, { I32 });
        b.memoryMin = 1;
        b.tableSize = 4;
        std::string callbacks = U32Bytes(4 + 4 * 9);
        for (int i = 0; i < 7; ++i) callbacks += U32Bytes(0);            // on_load ... on_restore
        callbacks += U32Bytes(1) + U32Bytes(alsoAtTheEnd ? 2 : 0);       // on_frame_begin, on_frame_end
        b.data.push_back({ 128, callbacks });
        b.Function(entry, Store(0, Get(1), I32Const(40)) + Store(4, Get(1), I32Const(1)) + Store(36, Get(1), I32Const(128)) + I32Const(0) + Op({ 0x0B }),
                   {}, "shard_plugin_entry");
        const std::vector<uint8_t> countDown = I32Const(1500) + Set(3) + Op({ 0x03, 0x40 }) + Get(3) + I32Const(1) + Op({ 0x6B, 0x22, 3, 0x0D, 0, 0x0B }) +
                                               I32Const(0) + Op({ 0x0B });
        const uint32_t begin = b.Function(frame, countDown, { { 1, I32 } });
        const uint32_t end = b.Function(frame, countDown, { { 1, I32 } });
        b.elements.push_back({ 1, { begin, end } });
        return b.Build();
    }

    class WasmFrameTest : public WasmPluginTest {
    protected:
        void RunFrames(int count)
        {
            pm().OnEngineSystemsReady(nullptr, &scheduler, &events);
            pm().OnEngineInitialized();
            Core::Ecs::Registry registry;
            Core::Ecs::Scheduler::FrameTiming timing;
            timing.deltaTime = 0.016f;
            for (int i = 0; i < count; ++i) {
                pm().BeginFrame(++frame);
                scheduler.RunFrame(registry, nullptr, timing);
                pm().EndFrame(frame);
            }
        }
        Core::Ecs::Scheduler scheduler;
        Events::EventDispatcher events;
        uint64_t frame = 0;
    };
}

TEST_F(WasmFrameTest, TheBudgetComesBackEveryFrame)
{
    Folder(FrameModule(false), { { "budgets", { { "frameMs", 0.0033 } } } });   // 10 000 units of fuel a frame
    ASSERT_TRUE(pm().Startup(config));
    RunFrames(5);
    EXPECT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
    pm().Shutdown();
}

TEST_F(WasmFrameTest, WhatRunsDuringOneFrameSharesItsBudget)
{
    // Each callback fits in the budget on its own, both do not
    Folder(FrameModule(true), { { "budgets", { { "frameMs", 0.0033 } } } });
    ASSERT_TRUE(pm().Startup(config));
    RunFrames(1);
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    pm().Shutdown();
}

namespace {
    // on_enable reads "hello.txt" of its content folder (5 bytes) : it fails unless the read worked and gave "hello"
    std::vector<uint8_t> ContentModule(const std::string& path)
    {
        Builder b;
        const uint32_t entry = b.Type({ I32, I32 }, { I32 });
        const uint32_t lifecycle = b.Type({ I32, I64 }, { I32 });
        b.Import("shard", "content.read", b.Type({ I64, I32, I32, I32 }, { I32 }));                     // 0
        b.memoryMin = 1;
        b.tableSize = 4;
        const std::string uri = std::string("mod://") + kId + "/" + path;
        b.data.push_back({ 300, uri });
        b.data.push_back({ 336, U32Bytes(300) + U32Bytes(0) + U64Bytes(uri.size()) });
        std::string callbacks = U32Bytes(4 + 4 * 9) + U32Bytes(0) + U32Bytes(0) + U32Bytes(1);
        for (int i = 0; i < 6; ++i) callbacks += U32Bytes(0);
        b.data.push_back({ 128, callbacks });
        b.Function(entry, Store(0, Get(1), I32Const(40)) + Store(4, Get(1), I32Const(1)) + Store(36, Get(1), I32Const(128)) + I32Const(0) + Op({ 0x0B }),
                   {}, "shard_plugin_entry");
        // result = read(...) ; if result != 0 return result ; return (load8_u(1024) - 'h') | (size != 5)
        const uint32_t onEnable = b.Function(lifecycle, Get(1) + I32Const(336) + I32Const(512) + I32Const(520) + Call(0) + Set(2) +
                                                        Get(2) + Op({ 0x04, 0x40 }) + Get(2) + Op({ 0x0F, 0x0B }) +
                                                        I32Const(512) + Op({ 0x28, 2, 0, 0x2D, 0, 0 }) + I32Const('h') + Op({ 0x6B }) +
                                                        I32Const(520) + Op({ 0x29, 3, 0 }) + I64Const(5) + Op({ 0x52, 0x72, 0x0B }), { { 1, I32 } });
        b.elements.push_back({ 1, { onEnable } });
        return b.Build();
    }

    json ContentManifest(bool permitted)
    {
        json patch = { { "kind", "module" }, { "moduleKind", "code" }, { "content", { { "mount", "content" } } } };
        if (permitted) patch["permissions"] = json::array({ "fs.read:content" });
        return patch;
    }
}

TEST_F(WasmPluginTest, ItReadsItsContentWithThePermission)
{
    Folder(ContentModule("hello.txt"), ContentManifest(true));
    dir.Write("p/content/hello.txt", "hello");
    std::string error;
    ASSERT_TRUE(pm().Startup(config, &error)) << error;
    pm().OnEngineInitialized();
    EXPECT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
    pm().Shutdown();
}

TEST_F(WasmPluginTest, WithoutThePermissionTheReadIsRefused)
{
    Folder(ContentModule("hello.txt"), ContentManifest(false));
    dir.Write("p/content/hello.txt", "hello");
    std::string error;
    ASSERT_TRUE(pm().Startup(config, &error)) << error;
    pm().OnEngineInitialized();
    EXPECT_EQ(Rec()->state, PluginState::Failed);                   // on_enable returned SHARD_E_UNSUPPORTED
    pm().Shutdown();
}

TEST_F(WasmPluginTest, APathOutOfItsContentFolderIsRefused)
{
    Folder(ContentModule("../shard-plugin.json"), ContentManifest(true));
    dir.Write("p/content/hello.txt", "hello");
    std::string error;
    ASSERT_TRUE(pm().Startup(config, &error)) << error;
    pm().OnEngineInitialized();
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    pm().Shutdown();
}

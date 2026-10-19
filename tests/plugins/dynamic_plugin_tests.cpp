#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "engine/core/ecs/registry.hpp"
#include "engine/plugins/manager.hpp"
#include "plugins/plugin_test_utils.hpp"

// Loads libraries for real : the plugin in tests/plugin_fixtures/test_plugin, built by shard_add_plugin next to the tests.

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using nlohmann::json;
using PluginTest::TempDir;

namespace {
    const char* kId = "shard.test.plugin";

    std::string FixtureDir() { return std::string(SHARD_TEST_PLUGIN_DIR) + "/" + kId; }

    class DynamicPluginTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            PluginManager::Get().ResetForTests();
            config.host = Host::Headless;
            config.registerConsoleCommands = false;
            config.pluginPaths.push_back(FixtureDir());
        }
        void TearDown() override { PluginManager::Get().ResetForTests(); }

        PluginManager& pm() { return PluginManager::Get(); }
        PluginRecord* Rec() { return pm().Find(kId); }

        void BringUpEngine()
        {
            pm().OnEngineSystemsReady(nullptr, &scheduler, &events);
            pm().OnEngineInitialized();
        }
        void RunFrames(int count)
        {
            Core::Ecs::Registry registry;
            Core::Ecs::Scheduler::FrameTiming timing;
            timing.deltaTime = 0.016f;
            for (int i = 0; i < count; ++i) {
                pm().BeginFrame(++frame);
                scheduler.RunFrame(registry, nullptr, timing);
                pm().EndFrame(frame);
            }
        }
        int Counter() { auto* v = Core::CVarRegistry::Global().Find("shard.test.plugin.counter"); return v ? std::stoi(v->ToString()) : -1; }

        PluginManagerConfig config;
        Core::Ecs::Scheduler scheduler;
        Events::EventDispatcher events;
        uint64_t frame = 0;
    };
}

TEST_F(DynamicPluginTest, TheManifestShardAddPluginWroteIsValid)
{
    std::ifstream in(FixtureDir() + "/shard-plugin.json");
    ASSERT_TRUE(in.good()) << FixtureDir();
    std::stringstream ss;
    ss << in.rdbuf();
    auto parsed = ParseManifestText(ss.str());
    ASSERT_TRUE(parsed) << parsed.GetError().message;
    EXPECT_EQ(parsed.Value().id, kId);
    EXPECT_FALSE(parsed.Value().BinaryForThisPlatform().empty());
    EXPECT_TRUE(parsed.Value().IsModule());
}

TEST_F(DynamicPluginTest, LoadsALibraryAndItsRegistrationsReachTheHost)
{
    std::string error;
    ASSERT_TRUE(pm().Startup(config, &error)) << error;
    ASSERT_NE(Rec(), nullptr);
    ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
    EXPECT_FALSE(Rec()->legacy);
    ASSERT_NE(Rec()->desc, nullptr);
    EXPECT_STREQ(Rec()->desc->fingerprint, kBuildFingerprint);

    EXPECT_TRUE(Objects::Components::GetComponentRegistry().IsRegistered("shard-test/Thing"));
    EXPECT_NE(Core::CVarRegistry::Global().Find("shard.test.plugin.counter"), nullptr);
    EXPECT_EQ(Core::CVarRegistry::Global().Execute("shardtest.ping").output, "pong");
    EXPECT_EQ(Core::CVarRegistry::Global().Find("shard.test.plugin.loads")->ToString(), "1");
    EXPECT_TRUE(pm().Services().Has("shardtest.pinger"));

    BringUpEngine();
    EXPECT_EQ(Rec()->state, PluginState::Enabled);
    RunFrames(3);
    EXPECT_EQ(Counter(), 3);          // the system ran in the library, and wrote to a cvar that lives in the host's registry
}

TEST_F(DynamicPluginTest, EverythingItRegisteredIsGoneOnceItIsUnloaded)
{
    ASSERT_TRUE(pm().Startup(config));
    BringUpEngine();
    RunFrames(1);
    pm().OnEngineDestroying();
    pm().Shutdown();
    EXPECT_FALSE(Objects::Components::GetComponentRegistry().IsRegistered("shard-test/Thing"));
    EXPECT_EQ(Core::CVarRegistry::Global().Find("shard.test.plugin.counter"), nullptr);
    EXPECT_FALSE(Core::CVarRegistry::Global().Execute("shardtest.ping").ok);
    EXPECT_FALSE(pm().Services().Has("shardtest.pinger"));
}

TEST_F(DynamicPluginTest, DisableThenEnableOnTheSameLibrary)
{
    ASSERT_TRUE(pm().Startup(config));
    BringUpEngine();
    RunFrames(2);
    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_FALSE(scheduler.Has("shard.test.plugin/tick"));
    EXPECT_EQ(Core::CVarRegistry::Global().Find("shard.test.plugin.counter"), nullptr);
    ASSERT_TRUE(pm().Enable(kId, &error)) << error;
    EXPECT_TRUE(scheduler.Has("shard.test.plugin/tick"));
    RunFrames(1);
    EXPECT_EQ(Counter(), 3);          // the plugin object kept its own count
}

TEST_F(DynamicPluginTest, ReloadKeepsTheStateThePluginSaved)
{
    config.allowHotReload = true;
    ASSERT_TRUE(pm().Startup(config));
    BringUpEngine();
    RunFrames(5);
    EXPECT_EQ(Counter(), 5);
    const std::string firstLoadedFrom = Rec()->loadedFrom;
    EXPECT_NE(firstLoadedFrom.find("shard_plugins_"), std::string::npos);          // a copy was loaded, not the original

    std::string error;
    ASSERT_TRUE(pm().Reload(kId, &error)) << error;
    EXPECT_EQ(Rec()->state, PluginState::Enabled);
    EXPECT_EQ(Rec()->reloadCount, 1u);
    EXPECT_NE(Rec()->loadedFrom, firstLoadedFrom);
    EXPECT_EQ(Counter(), 5);          // restored from the saved state
    // a fresh library : its own statics start over, and the load counter says so
    EXPECT_EQ(Core::CVarRegistry::Global().Find("shard.test.plugin.loads")->ToString(), "1");
    RunFrames(2);
    EXPECT_EQ(Counter(), 7);
    EXPECT_EQ(Core::CVarRegistry::Global().Execute("shardtest.ping").output, "pong");
}

TEST_F(DynamicPluginTest, ReloadIsRefusedWhenItIsOffOrTheRunIsDeterministic)
{
    ASSERT_TRUE(pm().Startup(config));
    BringUpEngine();
    std::string error;
    EXPECT_FALSE(pm().Reload(kId, &error));
    EXPECT_NE(error.find("hot reload"), std::string::npos);
    EXPECT_EQ(Rec()->state, PluginState::Enabled);

    pm().ResetForTests();
    config.allowHotReload = true;
    config.deterministic = true;
    ASSERT_TRUE(pm().Startup(config));
    EXPECT_FALSE(pm().Reload(kId, &error));
    EXPECT_NE(error.find("deterministic"), std::string::npos);
    // a deterministic run loads the library where it is, no copy
    EXPECT_EQ(Rec()->loadedFrom.find("shard_plugins_"), std::string::npos);
}

TEST_F(DynamicPluginTest, AReloadSetsTheWorldsAsideAndPutsThemBack)
{
    config.allowHotReload = true;
    ASSERT_TRUE(pm().Startup(config));
    BringUpEngine();
    int suspended = 0, resumed = 0;
    PluginManager::Callbacks callbacks;
    callbacks.hasLoadedWorlds = [] { return true; };
    callbacks.suspendWorlds = [&](std::string&) { ++suspended; return true; };
    callbacks.resumeWorlds = [&] { ++resumed; };
    pm().SetCallbacks(callbacks);
    std::string error;
    ASSERT_TRUE(pm().Reload(kId, &error)) << error;
    EXPECT_EQ(suspended, 1);
    EXPECT_EQ(resumed, 1);

    // nothing to set them aside with : refused, and the plugin is left alone
    PluginManager::Callbacks without;
    without.hasLoadedWorlds = [] { return true; };
    pm().SetCallbacks(without);
    EXPECT_FALSE(pm().Reload(kId, &error));
    EXPECT_EQ(Rec()->state, PluginState::Enabled);
    EXPECT_EQ(Rec()->reloadCount, 1u);
}

TEST_F(DynamicPluginTest, AComponentPluginCannotBeDisabledWhileAWorldIsLoaded)
{
    ASSERT_TRUE(pm().Startup(config));
    BringUpEngine();
    bool worlds = true;
    PluginManager::Callbacks callbacks;
    callbacks.hasLoadedWorlds = [&] { return worlds; };
    pm().SetCallbacks(callbacks);

    std::string error;
    EXPECT_FALSE(pm().Disable(kId, true, &error));
    EXPECT_NE(error.find("unload the worlds"), std::string::npos);
    EXPECT_EQ(Rec()->state, PluginState::Enabled);
    EXPECT_TRUE(Objects::Components::GetComponentRegistry().IsRegistered("shard-test/Thing"));

    worlds = false;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_FALSE(Objects::Components::GetComponentRegistry().IsRegistered("shard-test/Thing"));
}

TEST_F(DynamicPluginTest, ALibraryBuiltDifferentlyIsRefused)
{
    TempDir dir;
    std::filesystem::copy(FixtureDir(), dir.Sub("copy"), std::filesystem::copy_options::recursive);
    json manifest = json::parse(dir.Read("copy/shard-plugin.json"));
    manifest["abiFingerprint"] = "linux-x86_64|gcc1|debug|single|nodet";
    dir.Write("copy/shard-plugin.json", manifest.dump());
    config.pluginPaths = { dir.Sub("copy") };
    ASSERT_TRUE(pm().Startup(config));
    ASSERT_NE(Rec(), nullptr);
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    EXPECT_NE(Rec()->error.find("fingerprint"), std::string::npos) << Rec()->error;
    EXPECT_FALSE(Objects::Components::GetComponentRegistry().IsRegistered("shard-test/Thing"));
}

TEST_F(DynamicPluginTest, AManifestWithoutABinaryForThisPlatformIsLeftOut)
{
    TempDir dir;
    std::filesystem::copy(FixtureDir(), dir.Sub("copy"), std::filesystem::copy_options::recursive);
    json manifest = json::parse(dir.Read("copy/shard-plugin.json"));
    manifest["binaries"] = { { "plan9-mips", "x.so" } };
    dir.Write("copy/shard-plugin.json", manifest.dump());
    config.pluginPaths = { dir.Sub("copy") };
    ASSERT_TRUE(pm().Startup(config));
    EXPECT_EQ(Rec(), nullptr);
    bool reported = false;
    for (const Rejection& r : pm().Rejections()) if (r.id == kId && r.reason.find("no binary") != std::string::npos) reported = true;
    EXPECT_TRUE(reported);
}

TEST_F(DynamicPluginTest, ALibraryThatIsNotAPluginFailsCleanly)
{
    TempDir dir;
    std::filesystem::copy(FixtureDir(), dir.Sub("copy"), std::filesystem::copy_options::recursive);
    json manifest = json::parse(dir.Read("copy/shard-plugin.json"));
    manifest["entry"] = "no_such_entry";
    dir.Write("copy/shard-plugin.json", manifest.dump());
    config.pluginPaths = { dir.Sub("copy") };
    ASSERT_TRUE(pm().Startup(config));
    ASSERT_NE(Rec(), nullptr);
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    EXPECT_NE(Rec()->error.find("no_such_entry"), std::string::npos) << Rec()->error;
}

TEST_F(DynamicPluginTest, ALibraryGivenOnItsOwnBecomesAModule)
{
    std::string library;
    {
        std::ifstream in(FixtureDir() + "/shard-plugin.json");
        json manifest = json::parse(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
        library = FixtureDir() + "/" + manifest["binaries"][CurrentPlatformKey()].get<std::string>();
    }
    config.pluginPaths.clear();
    config.gameLibrary = library;
    ASSERT_TRUE(pm().Startup(config));
    PluginRecord* game = pm().Find("game");
    ASSERT_NE(game, nullptr);
    EXPECT_EQ(game->state, PluginState::Initialized) << game->error;
    EXPECT_TRUE(game->manifest.affectsSimulation);
    EXPECT_EQ(game->manifest.moduleKind, ModuleKind::Code);
}

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include <shard/shard_cvars.h>
#include <shard/shard_host.h>

#include "engine/core/config/cvar.hpp"
#include "engine/plugins/manager.hpp"
#include "plugins/plugin_test_utils.hpp"

// The C ABI end to end : the plugin in tests/plugin_fixtures/c_plugin is plain C, built against sdk/include/shard and linked to nothing of
// the engine. These tests load it for real.

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using nlohmann::json;
using PluginTest::TempDir;

namespace {
    const char* kId = "shard.test.cplugin";
    enum { EventLoad = 1, EventInitialize, EventEnable, EventDisable, EventUnload };

    std::string FixtureDir() { return std::string(SHARD_TEST_PLUGIN_DIR) + "/" + kId; }

    class CPluginTest : public ::testing::Test {
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

        template <typename Fn>
        Fn Export(const char* name) { return Rec()->library.Symbol<Fn>(name); }
        int Events() { return Export<int (*)()>("c_plugin_event_count")(); }
        std::vector<int> Order()
        {
            std::vector<int> events;
            auto at = Export<int (*)(int)>("c_plugin_event");
            for (int i = 0, n = Events(); i < n; ++i) events.push_back(at(i));
            return events;
        }

        // The fixture's folder copied with another entry point
        void UseEntry(TempDir& dir, const std::string& entry)
        {
            std::filesystem::copy(FixtureDir(), dir.Sub("copy"), std::filesystem::copy_options::recursive);
            json manifest = json::parse(dir.Read("copy/shard-plugin.json"));
            manifest["entry"] = entry;
            dir.Write("copy/shard-plugin.json", manifest.dump());
            config.pluginPaths = { dir.Sub("copy") };
        }

        PluginManagerConfig config;
    };

    // The helper's minimal manifest with some fields changed
    std::string ManifestText(const std::string& id, const json& patch = json::object())
    {
        json manifest = PluginTest::Manifest(id);
        manifest.merge_patch(patch);
        return manifest.dump();
    }
}

TEST_F(CPluginTest, TheManifestAsksForTheCAbi)
{
    auto parsed = ParseManifestText(ManifestText("com.acme.c", { { "abi", "c1" } }));
    ASSERT_TRUE(parsed) << parsed.GetError().message;
    EXPECT_EQ(parsed.Value().abi, "c1");
    EXPECT_EQ(parsed.Value().entry, "shard_plugin_entry");        // the default follows the ABI
    EXPECT_EQ(ManifestToJson(parsed.Value())["abi"], "c1");

    auto explicitEntry = ParseManifestText(ManifestText("com.acme.c", { { "abi", "c1" }, { "entry", "my_entry" } }));
    ASSERT_TRUE(explicitEntry);
    EXPECT_EQ(explicitEntry.Value().entry, "my_entry");
}

TEST_F(CPluginTest, TheCppAbiStaysTheDefault)
{
    auto parsed = ParseManifestText(ManifestText("com.acme.cpp"));
    ASSERT_TRUE(parsed) << parsed.GetError().message;
    EXPECT_EQ(parsed.Value().abi, "cpp");
    EXPECT_EQ(parsed.Value().entry, "shard_plugin_describe");
    EXPECT_FALSE(ManifestToJson(parsed.Value()).contains("abi"));
}

TEST_F(CPluginTest, AnUnknownAbiIsRejected)
{
    auto parsed = ParseManifestText(ManifestText("com.acme.x", { { "abi", "c9" } }));
    ASSERT_FALSE(parsed);
    EXPECT_NE(parsed.GetError().message.find("abi"), std::string::npos);
}

TEST_F(CPluginTest, ShardAddPluginWroteTheAbiIntoTheManifest)
{
    TempDir dir;
    std::filesystem::copy(FixtureDir(), dir.Sub("copy"), std::filesystem::copy_options::recursive);
    auto manifest = ParseManifestText(dir.Read("copy/shard-plugin.json"));
    ASSERT_TRUE(manifest) << manifest.GetError().message;
    EXPECT_EQ(manifest.Value().abi, "c1");
}

TEST_F(CPluginTest, LoadsAndRunsTheWholeLifecycle)
{
    std::string error;
    ASSERT_TRUE(pm().Startup(config, &error)) << error;
    ASSERT_NE(Rec(), nullptr);
    ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
    EXPECT_EQ(Rec()->desc, nullptr);                // no C++ descriptor : it went through the C ABI
    EXPECT_EQ(Order(), (std::vector<int>{ EventLoad, EventInitialize }));

    pm().OnEngineInitialized();
    EXPECT_EQ(Rec()->state, PluginState::Enabled);
    EXPECT_EQ(Order(), (std::vector<int>{ EventLoad, EventInitialize, EventEnable }));
    EXPECT_EQ(Export<int (*)()>("c_plugin_user_ok")(), 1);        // `user` came back untouched every time

    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_EQ(Order().back(), EventDisable);

    pm().Shutdown();
}

TEST_F(CPluginTest, TheInterfaceTheHostOffersDescribesThePlugin)
{
    ASSERT_TRUE(pm().Startup(config));
    ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
    EXPECT_STREQ(Export<const char* (*)()>("c_plugin_seen_id")(), kId);
    EXPECT_STREQ(Export<const char* (*)()>("c_plugin_seen_version")(), "1.0.0");
    EXPECT_STREQ(Export<const char* (*)()>("c_plugin_seen_host")(), "headless");      // shard.plugin 1.1
    EXPECT_EQ(Export<int (*)()>("c_plugin_has_net")(), SHARD_FALSE);
}

TEST_F(CPluginTest, MemoryFromTheHostIsCountedAndGivenBack)
{
    ASSERT_TRUE(pm().Startup(config));
    ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
    EXPECT_EQ(Rec()->context->Memory().Used(), 0u);
    pm().OnEngineInitialized();                      // on_enable allocates 1 KiB through the host
    EXPECT_GE(Rec()->context->Memory().Used(), 1024u);
    EXPECT_EQ(Export<int (*)()>("c_plugin_holds_block")(), 1);

    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;      // the context is gone with it : the plugin freed its block in on_disable
    EXPECT_EQ(Rec()->context, nullptr);
    EXPECT_EQ(Export<int (*)()>("c_plugin_holds_block")(), 0);
}

TEST_F(CPluginTest, ItCanBeDisabledAndEnabledAgain)
{
    ASSERT_TRUE(pm().Startup(config));
    pm().OnEngineInitialized();
    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    ASSERT_TRUE(pm().Enable(kId, &error)) << error;
    EXPECT_EQ(Rec()->state, PluginState::Enabled);
    EXPECT_EQ(Export<int (*)()>("c_plugin_holds_block")(), 1);
}

TEST_F(CPluginTest, AnAbiRevisionNewerThanTheHostIsRefused)
{
    TempDir dir;
    UseEntry(dir, "shard_plugin_entry_future");
    ASSERT_TRUE(pm().Startup(config));
    ASSERT_NE(Rec(), nullptr);
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    const std::string newer = "C ABI " + std::to_string(SHARD_ABI_MAJOR) + "." + std::to_string(SHARD_ABI_MINOR + 1);
    EXPECT_NE(Rec()->error.find(newer), std::string::npos) << Rec()->error;
}

TEST_F(CPluginTest, AnotherMajorAbiIsRefused)
{
    TempDir dir;
    UseEntry(dir, "shard_plugin_entry_other_major");
    ASSERT_TRUE(pm().Startup(config));
    ASSERT_NE(Rec(), nullptr);
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    EXPECT_NE(Rec()->error.find("C ABI 2." + std::to_string(SHARD_ABI_MINOR)), std::string::npos) << Rec()->error;
}

TEST_F(CPluginTest, AStructTooSmallIsRefused)
{
    TempDir dir;
    UseEntry(dir, "shard_plugin_entry_small");
    ASSERT_TRUE(pm().Startup(config));
    ASSERT_NE(Rec(), nullptr);
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    EXPECT_NE(Rec()->error.find("too small"), std::string::npos) << Rec()->error;
}

TEST_F(CPluginTest, NoCallbacksTableIsRefused)
{
    TempDir dir;
    UseEntry(dir, "shard_plugin_entry_no_callbacks");
    ASSERT_TRUE(pm().Startup(config));
    ASSERT_NE(Rec(), nullptr);
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    EXPECT_NE(Rec()->error.find("callbacks"), std::string::npos) << Rec()->error;
}

TEST_F(CPluginTest, AnEntryPointThatRefusesFailsThePlugin)
{
    TempDir dir;
    UseEntry(dir, "shard_plugin_entry_refuse");
    ASSERT_TRUE(pm().Startup(config));
    ASSERT_NE(Rec(), nullptr);
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    EXPECT_NE(Rec()->error.find("refused"), std::string::npos) << Rec()->error;
}

TEST_F(CPluginTest, ALibraryWithoutTheEntryPointIsNamedInTheError)
{
    TempDir dir;
    UseEntry(dir, "no_such_entry");
    ASSERT_TRUE(pm().Startup(config));
    ASSERT_NE(Rec(), nullptr);
    EXPECT_EQ(Rec()->state, PluginState::Failed);
    EXPECT_NE(Rec()->error.find("no_such_entry"), std::string::npos) << Rec()->error;
}

TEST_F(CPluginTest, TheBuildFingerprintDoesNotApply)
{
    // A C plugin is compiled by anybody : a manifest fingerprint that is not ours must not stop it
    TempDir dir;
    std::filesystem::copy(FixtureDir(), dir.Sub("copy"), std::filesystem::copy_options::recursive);
    json manifest = json::parse(dir.Read("copy/shard-plugin.json"));
    manifest["abiFingerprint"] = "linux-x86_64|gcc1|debug|single|nodet";
    dir.Write("copy/shard-plugin.json", manifest.dump());
    config.pluginPaths = { dir.Sub("copy") };
    ASSERT_TRUE(pm().Startup(config));
    ASSERT_NE(Rec(), nullptr);
    EXPECT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
}

// ---- shard.cvars ----

namespace {
    Core::CVarBase* Var(const char* name) { return Core::CVarRegistry::Global().Find(std::string("shard.test.cplugin.") + name); }
}

TEST_F(CPluginTest, CVarsCreatedFromCAreInTheHostsRegistry)
{
    ASSERT_TRUE(pm().Startup(config));
    ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
    EXPECT_EQ(Export<int (*)()>("c_plugin_cvar_error")(), 0);       // four created, the duplicate name refused with SHARD_E_ALREADY_EXISTS
    ASSERT_NE(Var("counter"), nullptr);
    ASSERT_NE(Var("note"), nullptr);
    ASSERT_NE(Var("scale"), nullptr);
    ASSERT_NE(Var("locked"), nullptr);
    EXPECT_EQ(Var("counter")->ToString(), "0");
    EXPECT_EQ(Var("note")->ToString(), "hello");
    EXPECT_EQ(Var("scale")->ToString(), "1.5");
    EXPECT_EQ(uint32_t(Var("locked")->Flags()) & uint32_t(Core::CVarFlags::ReadOnly), 1u);
    EXPECT_EQ(uint32_t(Var("counter")->Flags()) & uint32_t(Core::CVarFlags::ReadOnly), 0u);
}

TEST_F(CPluginTest, TheCodeReadsAndWritesItsCVars)
{
    ASSERT_TRUE(pm().Startup(config));
    pm().OnEngineInitialized();                                     // on_enable : counter + 1, note, scale above its maximum, locked
    ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
    EXPECT_EQ(Var("counter")->ToString(), "1");
    EXPECT_EQ(Export<int (*)()>("c_plugin_cvar_counter")(), 1);
    EXPECT_EQ(Var("note")->ToString(), "enabled");
    EXPECT_EQ(Var("scale")->ToString(), "2");                       // clamped to the maximum the plugin gave
    EXPECT_EQ(Var("locked")->ToString(), "false");                  // the owner can change a read-only variable
}

TEST_F(CPluginTest, AHandleIsStaleOnceTheHostDestroyedTheVariable)
{
    ASSERT_TRUE(pm().Startup(config));
    pm().OnEngineInitialized();
    EXPECT_EQ(Export<int (*)()>("c_plugin_read_old_counter")(), SHARD_OK);
    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_EQ(Var("counter"), nullptr);                             // gone with the plugin's context
    EXPECT_EQ(Export<int (*)()>("c_plugin_read_old_counter")(), SHARD_E_STALE_HANDLE);

    ASSERT_TRUE(pm().Enable(kId, &error)) << error;                 // on_load makes them again, with new handles
    ASSERT_NE(Var("counter"), nullptr);
    EXPECT_EQ(Export<int (*)()>("c_plugin_read_old_counter")(), SHARD_OK);
    EXPECT_EQ(Export<int (*)()>("c_plugin_cvar_error")(), 0);
}

// ---- shard.commands ----

TEST_F(CPluginTest, ACommandWrittenInCRunsFromTheConsole)
{
    ASSERT_TRUE(pm().Startup(config));
    ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
    EXPECT_EQ(Export<int (*)()>("c_plugin_command_error")(), 0);        // the second registration of the name was refused

    auto& console = Core::CVarRegistry::Global();
    const Core::CommandResult ok = console.Execute("cplugin.echo hello world");
    EXPECT_TRUE(ok.ok) << ok.output;
    EXPECT_EQ(ok.output, "echo:hello:2");                              // the words reached the plugin as ShardStr, the text came back

    const Core::CommandResult bad = console.Execute("cplugin.echo");
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.output, "usage: cplugin.echo <word>");

    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_FALSE(console.Execute("cplugin.echo hello").ok);             // removed with the plugin
}

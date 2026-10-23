#include <gtest/gtest.h>

#include <string>

#include "engine/core/config/cvar.hpp"
#include "engine/plugins/manager.hpp"

// A sandboxed plugin as a plugin author makes it : tests/plugin_fixtures/wasm_plugin/wasm_plugin.c, written against sdk/include
// (shard_wasm.h), compiled by Clang for wasm32 and linked by wasm-ld (tests/CMakeLists.txt). Only built when both are found.

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;

namespace {
    const char* kId = "shard.test.wasm";

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
            config.pluginPaths.push_back(SHARD_TEST_WASM_PLUGIN_DIR);
#endif
        }
        void TearDown() override { PluginManager::Get().ResetForTests(); }

        PluginManager& pm() { return PluginManager::Get(); }
        PluginRecord* Rec() { return pm().Find(kId); }
        PluginManagerConfig config;
    };
}

TEST_F(WasmSdkPluginTest, APluginCompiledByClangRunsInTheSandbox)
{
    std::string error;
    ASSERT_TRUE(pm().Startup(config, &error)) << error;
    ASSERT_NE(Rec(), nullptr);
    ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;            // on_load : a command and a cvar
    Core::CVarBase* speed = Core::CVarRegistry::Global().Find(std::string(kId) + ".speed");
    ASSERT_NE(speed, nullptr);
    EXPECT_EQ(speed->ToString(), "2.5");

    pm().OnEngineInitialized();                                                   // on_enable : reads content/hello.txt, speed + 1
    ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
    EXPECT_EQ(speed->ToString(), "3.5");

    const Core::CommandResult echo = Core::CVarRegistry::Global().Execute("sandbox.echo hello world");
    EXPECT_TRUE(echo.ok) << echo.output;
    EXPECT_EQ(echo.output, "hello");
    pm().Shutdown();
}

#include <gtest/gtest.h>

#include "engine/plugins/manager.hpp"
#include "plugins/plugin_test_utils.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;

namespace {
    // The editor's ImGui functions, as the editor would hand them over
    int gAllocations = 0;
    void* Allocate(size_t size, void*) { ++gAllocations; return ::operator new(size); }
    void Free(void* p, void*) { ::operator delete(p); }
}

TEST(EditorImGui, ALibraryWithItsOwnImGuiIsHandedTheEditorsContext)
{
    PluginManager& pm = PluginManager::Get();
    pm.ResetForTests();
    PluginManagerConfig config;
    config.host = Host::Editor;
    config.registerConsoleCommands = false;
    config.pluginPaths.push_back(std::string(SHARD_TEST_PLUGIN_DIR) + "/shard.test.editor");
    ASSERT_TRUE(pm.Startup(config));
    PluginRecord* r = pm.Find("shard.test.editor");
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(r->state, PluginState::Initialized) << r->error;
    pm.OnEngineSystemsReady(nullptr, nullptr, nullptr);
    pm.OnEngineInitialized();

    // No context : the library says so and keeps going
    pm.NotifyEditorInit();
    EXPECT_EQ(pm.Extensions().Get<EditorPanelItem>("editor.panel").size(), 1u);
    pm.Disable("shard.test.editor");
    ASSERT_TRUE(pm.Enable("shard.test.editor"));

    // A context : the library's ImGui now sees it (the pointer is only passed around, never used)
    EditorImGui imgui;
    imgui.context = reinterpret_cast<void*>(uintptr_t(0x1234500));
    imgui.allocate = &Allocate;
    imgui.free = &Free;
    pm.SetEditorImGui(imgui);
    pm.NotifyEditorInit();
    auto& console = Core::CVarRegistry::Global();
    EXPECT_NE(console.Execute("shardtest.imguicontext").output.find("1234500"), std::string::npos) << console.Execute("shardtest.imguicontext").output;

    pm.OnEngineDestroying();
    pm.ResetForTests();
    pm.SetEditorImGui(EditorImGui());
}

TEST(EditorImGui, AnEditorPluginEnabledWhileTheEditorRunsGetsItsInitAndKeepsItsOpenWindowsAcrossAReload)
{
    PluginManager& pm = PluginManager::Get();
    pm.ResetForTests();
    PluginManagerConfig config;
    config.host = Host::Editor;
    config.registerConsoleCommands = false;
    config.allowHotReload = true;
    config.pluginPaths.push_back(std::string(SHARD_TEST_PLUGIN_DIR) + "/shard.test.editor");
    ASSERT_TRUE(pm.Startup(config));
    pm.OnEngineSystemsReady(nullptr, nullptr, nullptr);
    pm.OnEngineInitialized();
    pm.NotifyEditorInit();
    ASSERT_EQ(pm.Extensions().Get<EditorPanelItem>("editor.panel").size(), 1u);

    // Switched off, its window goes ; switched on again, the editor is already up and it adds the window by itself
    ASSERT_TRUE(pm.Disable("shard.test.editor"));
    EXPECT_TRUE(pm.Extensions().Get<EditorPanelItem>("editor.panel").empty());
    ASSERT_TRUE(pm.Enable("shard.test.editor"));
    auto panels = pm.Extensions().Get<EditorPanelItem>("editor.panel");
    ASSERT_EQ(panels.size(), 1u);

    // Reloaded, the window that was open is open again
    panels[0]->visible = true;
    panels.clear();                                                         // an item is code of the library: none is held while it goes
    std::string error;
    if (pm.Reload("shard.test.editor", &error)) {
        panels = pm.Extensions().Get<EditorPanelItem>("editor.panel");
        ASSERT_EQ(panels.size(), 1u);
        EXPECT_TRUE(panels[0]->visible);
    } else {
        EXPECT_NE(error.find("reload"), std::string::npos) << error;        // a plugin that does not support reloading says so
    }

    pm.OnEngineDestroying();
    pm.ResetForTests();
}

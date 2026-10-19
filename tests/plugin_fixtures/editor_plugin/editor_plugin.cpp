// An editor plugin that draws with its own copy of ImGui, loaded by tests/plugins/editor_imgui_tests.cpp.
#include "engine/plugins/context.hpp"
#include "engine/plugins/editor_imgui.hpp"
#include "engine/plugins/plugin.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;

class TestEditorPlugin final : public EditorPlugin {
public:
    void OnLoad(PluginContext& ctx) override
    {
        ctx.Commands().Add("shardtest.imguicontext", "the ImGui context this library sees", [](const std::vector<std::string>&) {
            char text[64];
            std::snprintf(text, sizeof(text), "%p", static_cast<void*>(ImGui::GetCurrentContext()));
            return Core::CommandResult{ true, text };
        });
    }

    void OnEditorInit(EditorContext& editor) override
    {
        m_Bound = BindEditorImGui(editor);
        AddEditorPanel(editor.plugin, "Test editor panel", [](bool*) {});
        editor.plugin.Log(m_Bound ? "ImGui bound" : "no ImGui context");
    }

private:
    bool m_Bound = false;
};

SHARD_PLUGIN(TestEditorPlugin)

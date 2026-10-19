#pragma once

// For an editor plugin that draws with ImGui. Include it after your ImGui setup is in the plugin's sources (shard_add_plugin ... EDITOR_UI adds ImGui to the
// library). A library has its own copy of ImGui's globals : the editor's context and allocators are handed over here, once, from OnEditorInit.
//
//     void OnEditorInit(EditorContext& editor) override
//     {
//         BindEditorImGui(editor);
//         AddEditorPanel(editor.plugin, "My window", [](bool* open) { if (ImGui::Begin("My window", open)) { ImGui::Text("hello"); } ImGui::End(); });
//     }

#include "imgui/imgui.h"

#include "engine/plugins/plugin.hpp"

namespace Shard::Engine::Plugins {

    /// @return false if the editor gave no context (a run without a user interface)
    inline bool BindEditorImGui(const EditorContext& editor)
    {
        if (!editor.imgui.context) return false;
        ImGui::SetAllocatorFunctions(reinterpret_cast<ImGuiMemAllocFunc>(editor.imgui.allocate), reinterpret_cast<ImGuiMemFreeFunc>(editor.imgui.free), editor.imgui.userData);
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(editor.imgui.context));
        return true;
    }
}

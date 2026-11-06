#pragma once

// A library has its own copy of ImGui's globals, so the editor's context and allocators are handed over once from OnEditorInit.

#include "imgui/imgui.h"

#include "engine/plugins/plugin.hpp"

namespace Shard::Engine::Plugins {

    inline bool BindEditorImGui(const EditorContext& editor)
    {
        if (!editor.imgui.context) return false;
        ImGui::SetAllocatorFunctions(reinterpret_cast<ImGuiMemAllocFunc>(editor.imgui.allocate), reinterpret_cast<ImGuiMemFreeFunc>(editor.imgui.free), editor.imgui.userData);
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(editor.imgui.context));
        return true;
    }
}

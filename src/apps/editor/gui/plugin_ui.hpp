#pragma once

// The widgets the C plugins draw their editor panels with (sdk/include/shard/shard_editor.h) : ImGui calls behind a table that has no ImGui
// in it. Installed once the editor has its ImGui context, removed before the context goes.

namespace Shard::Editor::GUI {

    void InstallPluginUi();
    void RemovePluginUi();
}

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

struct ShardUiApi;
namespace Shard::Engine::Objects { class Actor; }
namespace Shard::Engine::Plugins { struct EditorDropEvent; }
namespace Shard::Editor::Core { class EditorMainWindow; }

namespace Shard::Editor::GUI {

    void InstallPluginUi(Core::EditorMainWindow* window);
    void RemovePluginUi();

    struct PathEntry {
        std::string path;
        std::function<void()> run;
    };
    /// The entries of a menu, grouped in submenus by the '/' of their path
    void DrawPathEntries(const std::vector<PathEntry>& entries);

    void DrawPluginMenuItems(const char* menu, bool separator = true);
    std::vector<std::string> PluginMenuNames();
    void DrawPluginToolbar();
    void DrawPluginInspectors(const std::string& componentType, const std::shared_ptr<Engine::Objects::Actor>& actor);

    const ShardUiApi* PluginWidgets();

    // ---- Asset editors of plugins (shard.editor 1.4) : one window per open asset, managed here ----
    /// True when a plugin registered an editor for this asset's extension
    bool HasPluginAssetEditor(const std::string& pathInProject);
    /// Opens (or focuses) the window of the asset. False when no plugin has an editor for it.
    bool OpenPluginAssetEditor(const std::string& pathInProject);
    bool SetPluginAssetEditorDirty(const std::string& pathInProject, bool dirty);
    /// Draws the open windows, once per frame
    void DrawPluginAssetEditors();

    // ---- Drop hooks of plugins : true when a plugin took the drop, the editor then does nothing with it ----
    bool DispatchPluginDrop(const Engine::Plugins::EditorDropEvent& event);

    void DrawPluginGizmos(const glm::mat4& viewProjection, const float min[2], const float max[2],
                          const std::vector<std::shared_ptr<Engine::Objects::Actor>>& selected);
}

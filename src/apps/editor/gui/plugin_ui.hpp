#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

struct ShardUiApi;
namespace Shard::Engine::Objects { class Actor; }
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

    void DrawPluginGizmos(const glm::mat4& viewProjection, const float min[2], const float max[2],
                          const std::vector<std::shared_ptr<Engine::Objects::Actor>>& selected);
}

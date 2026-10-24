#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

struct ShardUiApi;
namespace Shard::Engine::Objects { class Actor; }
namespace Shard::Editor::Core { class EditorMainWindow; }

// What the plugins draw the editor's windows, inspectors and gizmos with (sdk/include/shard/shard_editor.h) : ImGui calls behind tables that have
// no ImGui in them. Installed once the editor has its ImGui context, removed before the context goes.

namespace Shard::Editor::GUI {

    /// `window` : whose selection and notifications the plugins reach
    void InstallPluginUi(Core::EditorMainWindow* window);
    void RemovePluginUi();

    /// The items plugins added to the menu bar under the menu `menu` ("Tools", "Window"...), drawn inside it, as nested menus where their path has more parts
    void DrawPluginMenuItems(const char* menu, bool separator = true);
    /// The menus of the menu bar plugins added that the editor does not have itself, in the order they were first used ; each is drawn with DrawPluginMenuItems
    std::vector<std::string> PluginMenuNames();
    /// The buttons plugins added to the viewport's toolbar
    void DrawPluginToolbar();
    /// What plugins draw below the component `componentType` of `actor` in the Properties panel (empty type : below all of them)
    void DrawPluginInspectors(const std::string& componentType, const std::shared_ptr<Engine::Objects::Actor>& actor);

    /// The widgets, for the inspectors the Properties panel calls
    const ShardUiApi* PluginWidgets();

    /// Calls the gizmos plugins added for each selected actor, over the image of the viewport (`min` and `max` : its corners on the screen).
    /// Inside the viewport's window, once the image is drawn.
    void DrawPluginGizmos(const glm::mat4& viewProjection, const float min[2], const float max[2],
                          const std::vector<std::shared_ptr<Engine::Objects::Actor>>& selected);
}

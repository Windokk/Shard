// What a plugin adds to the editor besides a panel (shard.editor 1.1) : a menu item, a toolbar button, an inspector, a gizmo, the selection and
// a window that uses the richer widgets. Only added when the host is the editor.
#include <shard/game.hpp>

#include "stats.hpp"

SHARD_EDITOR_MENU_ITEM(BakeAll, "Tools/Demo/Bake all") { ++GetStats().menuRuns; }

SHARD_EDITOR_TOOLBAR_BUTTON(Ping, "Ping")
{
    ++GetStats().toolbarRuns;
    Shard::Editor(Shard::detail::PluginHandle()).Notify("pinged", SHARD_EDITOR_NOTIFY_WARNING);
}

// Below every component of the actor the Properties panel shows
SHARD_EDITOR_INSPECTOR(Everything, "", ui, actor)
{
    Stats& s = GetStats();
    ++s.inspectorDraws;
    s.inspected = actor.Name();
    ui.SeparatorText("Demo");
    static bool enabled = true;
    ui.Checkbox("Enabled", enabled);
    ui.BeginDisabled(!enabled);
    Shard::Vec3 offset{ 1.f, 2.f, 3.f };
    ui.DragVec3("Offset", offset);
    ui.EndDisabled();
}

SHARD_EDITOR_GIZMO(Axes, gizmo, actor)
{
    Stats& s = GetStats();
    ++s.gizmoDraws;
    s.gizmoActor = actor.Name();
    const Shard::Pose pose = actor.WorldPose();
    gizmo.Line(pose.position, Shard::Vec3{ pose.position.x + 1.f, pose.position.y, pose.position.z }, Shard::Color{ 1.f, 0.f, 0.f, 1.f });
    gizmo.Sphere(pose.position, 0.5f);
    gizmo.Text(pose.position, "axes");
}

SHARD_ON_LOAD(EditorDemo, ctx)
{
    ctx.Editor().OnSelectionChanged([](const std::vector<Shard::Actor>& selected) {
        ++GetStats().selectionEvents;
        GetStats().selectionSize = int(selected.size());
    });
}

// The richer widgets: tabs, a table, a menu, a popup, a child region
SHARD_EDITOR_PANEL(Widgets, "Demo widgets", ui)
{
    Stats& s = GetStats();
    ++s.widgetDraws;
    static int mode = 1;
    static int picked = 0;
    static std::string note = "n";
    ui.RadioButton("one", mode, 0);
    ui.RadioButton("two", mode, 1);
    if (ui.BeginTabBar("tabs")) {
        if (ui.BeginTabItem("First")) {
            ++s.tabItems;
            ui.Combo("Pick", picked, { "a", "b", "c" });
            ui.InputTextMultiline("Note", note);
            ui.EndTabItem();
        }
        ui.EndTabBar();
    }
    if (ui.BeginTable("table", 2)) {
        ui.TableSetupColumn("Name");
        ui.TableSetupColumn("Value");
        ui.TableHeadersRow();
        for (int row = 0; row < 2; ++row) {
            ui.TableNextRow();
            ui.TableNextColumn();
            ui.Text("row");
            ui.TableNextColumn();
            ++s.tableCells;
            ui.Text("cell");
        }
        ui.EndTable();
    }
    ui.Button("Menu");
    if (ui.BeginPopupContextItem()) {
        if (ui.MenuItem("Do", "Ctrl+D")) ++s.menuRuns;
        ui.EndPopup();
    }
    if (ui.BeginChild("child", 0.f, 40.f, true)) ui.TextDisabled("inside");
    ui.EndChild();
    s.pickedIndex = picked;
    s.contentWidth = ui.ContentWidth();
}

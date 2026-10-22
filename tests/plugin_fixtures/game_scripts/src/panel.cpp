// A panel of the editor : added only when the host is the editor
#include <shard/game.hpp>

#include "stats.hpp"

SHARD_EDITOR_PANEL(GameDebug, "Game debug", ui)
{
    static int count = 3;
    static std::string name = "bob";
    ++GetStats().panelDraws;
    ui.Text("hello");
    ui.SliderInt("Count", count, 1, 10);
    ui.InputText("Name", name);
    if (ui.Button("Spawn")) ++GetStats().panelClicks;
}

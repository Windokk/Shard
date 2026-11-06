# Editor

`ShardEditor` (`src/apps/editor`) is the tool you build a game in : it opens a project, lets you edit its worlds, assets and settings, and runs the game inside its viewport so you can test a change without leaving it.

It is the same engine as [the player](player.md), with an [ImGui](https://github.com/ocornut/imgui) interface drawn on top of the Vulkan backend. The game module (the shared library holding your scripts and components) is loaded into it with `--game`, so the editor knows your reflected types and can show them in the properties panel.

## Role

| The editor... | ...does not |
|---|---|
| edits worlds, actors, components and assets | ship with your game : players get `ShardGame` |
| runs the game in play mode, inside the viewport | write replays or benchmarks : that is the player's job |
| saves the project and its worlds | run without a window : use [ShardHeadless](../headless.md) |

## Launching

```
ShardEditor --game plugins/game/bin/linux-x86_64/libGameModule.so --project example_project.json
```

`--project` is required : with none, the editor prints `No project specified, aborting...` and stops. `--game` is the game module to load (`GameModule.dll` on Windows).

## Command line

| Option | |
|---|---|
| `--project <file>` | project to open (required) |
| `--game <module>` | game module to load |
| `--width <n>` `--height <n>` | initial window size |
| `--fullscreen` | starts fullscreen |
| `--vsync <0\|1>` | vertical sync |
| `--api <opengl\|vulkan\|dx11\|dx12>` | graphics API |
| `--gravity <x> <y> <z>` | overrides the world's gravity |
| `--debug <log\|info\|warning\|error\|fatal>` | lowest log level printed (default `log`) |
| `--script <file>` | runs console commands at given frames, one `<frame> <command>` per line |
| `--frames <n>` | runs `n` frames, then closes without waiting for Enter : a non-interactive run, for smoke tests |
| `+name=value` or `+name value` | sets a console variable before the engine starts (`+engine.maxFPS=60`) |

Without `--frames`, the editor waits for Enter before closing its console window.

## Interface

Every panel is toggled from the **Window** menu.

| Panel | What it does |
|---|---|
| Viewport | the scene, with the gizmos, the scene camera, the view modes and the visibility toggles. Also shows the statistics overlay and the offline raytrace controls |
| World Tree | the actors of the loaded worlds, as a tree with folders. Select (several at once), rename, group, drag to reparent, copy / paste |
| Properties | the reflected fields of the selected actors and their components. An edit is one undo step |
| World Settings | settings of the loaded world |
| Asset Browser | the project's assets : import, create, rename, reveal in the file explorer, drag into the viewport or the tree |
| Console | runs [console commands and variables](../time.md#time-scale) (`timescale 0.5`), and shows the log |
| Profiler | the time of the last frames by category, CPU and GPU |
| Project Settings | physics step, rendering and other project-wide values |
| Editor Preferences | how the editor itself behaves, stored per user and not in the project |

Double-clicking an asset opens its own editor when there is one (the material editor, for example).

## Editing

* **Undo / Redo** : every change goes through the command stack (`Ctrl+W` / `Ctrl+Y`), so a drag in the viewport or an edit in the properties panel can be taken back.
* **Tools** : Move, Rotate and Scale gizmos ; `F` focuses the selection, `End` snaps it to the floor.
* **Selection** : `Ctrl+A` selects all, `Esc` deselects, and the Select menu inverts the selection or selects the children.
* **Clipboard** : `Ctrl+C`, `Ctrl+X`, `Ctrl+V`, `Ctrl+D` duplicates, `F2` renames, `Ctrl+G` groups, `Del` deletes.
* **Save** : `Ctrl+S` saves the world.

## Play mode

**Tools → Play Mode** starts the game inside the viewport : scripts get `OnPlay`, then their ticks (see [Time](../time.md)), and `OnStop` when you stop. Pressing Play first saves the first loaded world to its file ; stopping reloads it from there. What a script moves or destroys while playing is therefore thrown away, and the world comes back as it was when you pressed Play. **F1** stops the play mode (the only editor shortcut that works while playing).

The accumulator is emptied while not in play mode, so pressing Play does not replay a burst of fixed steps.

## Exit

The editor shuts the interface down, destroys the engine, then exits with code `0`.

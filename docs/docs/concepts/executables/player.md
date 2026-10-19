# Player

`ShardGame` (`src/apps/player`) is the executable a game ships with. It loads a project and a game module, starts the first world in play mode and runs it with a window, the renderer and the audio. It has no editing interface : that is [the editor](editor.md).

It is also the tool for everything that needs the real game on a real GPU : smoke tests, benchmarks, screenshots, and recording sessions for [replays](../headless.md).

!!! note
    The CMake target is `ShardGame` (the source folder is `src/apps/player`). It is built as a Windows GUI application (`WIN32_EXECUTABLE`), so on Windows it has no console of its own : run it from a terminal to see the output.

## Launching

```
ShardGame --game libGameModule.so --project example_project.json
```

`--project` is required. If no world can be loaded (a bad path, a broken file), the player stops with `No world was loaded, aborting...`.

## Command line

### Window and rendering

| Option | |
|---|---|
| `--project <file>` | project to run (required) |
| `--game <module>` | game module to load |
| `--world <path>` | world to start with (default : the project's first world) |
| `--width <n>` `--height <n>` | window size |
| `--fullscreen` | starts fullscreen |
| `--vsync <0\|1>` | vertical sync |
| `--api <opengl\|vulkan\|dx11\|dx12>` | graphics API |
| `--gravity <x> <y> <z>` | overrides the world's gravity |
| `--debug <log\|info\|warning\|error\|fatal>` | lowest log level printed (default `log`) |
| `--view-mode <mode>` | debug view of the scene, as in the editor's viewport : `lit`, `unlit`, `wireframe`, `shaded-wireframe`, `normals`, `depth`, `uvs`, `gi`, `ssao`, `lightmap-uvs`, `shadow-cascades` |
| `+name=value` or `+name value` | sets a console variable before the engine starts (`+engine.maxFPS=60`) |
| `--script <file>` | runs console commands at given frames, one `<frame> <command>` per line |

### Non-interactive runs

| Option | |
|---|---|
| `--frames <n>` | renders `n` frames, then shuts down. Nothing waits for Enter, the project is not written to (read-only) and the exit code tells whether it went well. This is what the CI smoke test uses (`scripts/smoke_test.sh`) |
| `--screenshot <file.png>` | writes the last rendered frame (the viewport, before presentation) to a PNG |
| `--bench` | with `--frames`, prints the average, median, p99 and worst frame time, the time per category, the render thread stats, the scene size and the RAM / VRAM in use, as `[BENCH]` lines |
| `--bench-warmup <n>` | frames left out of the benchmark at the start, for shader compiles and streaming (default 120) |

```
ShardGame --game libGameModule.so --project example_project.json --frames 600 --bench --screenshot out.png
```

### Record and replay

| Option | |
|---|---|
| `--record <file>` | records the session's inputs, and the state of the simulation every `--check-every` frames, to a replay file |
| `--replay <file>` | plays a replay back : the keyboard is ignored, the file's inputs are fed to the game and its state checks are verified |
| `--seed <n>` | seed the random streams derive from (default : the replay's, otherwise 1) |
| `--check-every <n>` | frames between two state checks (default 60) |

Recording or replaying makes the run [deterministic](../headless.md#determinism) : every frame lasts exactly one fixed step, and the project is left as it was found. A replay recorded with a different fixed step than the project's is refused. Playing a replay without `--frames` runs for as long as the replay does.

## Exit

Without `--frames`, the player prints `Shard Engine has finished. Press Enter to exit...` and waits, so the output stays readable when it was started by double-click.

| Code | Meaning |
|---|---|
| `0` | success (and the replay matched, if there was one) |
| `1` | a replay could not be saved, or the engine failed to start |
| `2` | the replay diverged : the simulation state differs from the recorded one |

With `--frames`, a crash at startup exits with `1` right away instead of waiting for Enter.

## Main loop

Each iteration of the loop calls `Engine::Run()` once : the scheduler goes through its phases (`Input → PreSim → Fixed × N → PostPhysics → Update → Late → Extract`, see [Time](../time.md)), then the frame is presented. In a replay, the file's events are posted to the window before the frame and the state is checked after it.

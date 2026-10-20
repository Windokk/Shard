# Headless mode and determinism

A project's simulation can run with no window, GPU or sound card, and it can be made deterministic: the same inputs and seed give the same state on every frame, on Windows and Linux. This is used for:

- dedicated servers that run the simulation without rendering
- simulation regression tests in CI, replaying a recorded run and checking its state along the way
- replays, since a session's inputs take only a few KB

```
ShardHeadless --game plugins/game/bin/windows-x86_64/libGameModule.dll --project example_project.json --replay run.replay
```

## ShardHeadless

`ShardHeadless` (`src/apps/headless`) is the player without a screen: same engine, same game module, same simulation code. It uses the headless platform (a fake window that only has a size and an event queue, SDL is never initialized) and the null graphics backend.

| Option | |
|---|---|
| `--game <module>` `--project <file>` | game module and project (required) |
| `--world <path>` | world to load (default: the replay's, otherwise the project's first world) |
| `--frames <n>` | number of frames to run (default: replay length, otherwise 600) |
| `--seed <n\|random>` | seed for the random streams (default: the replay's, otherwise 1) |
| `--replay <file>` | plays back a replay, feeding its inputs to the game and verifying its state checks |
| `--write-replay <file>` | records this run to a replay. Use it to create one, or to update the checks after a change that is meant to affect the simulation |
| `--check-every <n>` | adds a state check every `n` frames (default 60) |
| `--expect <hash>` | expected state hash after the last frame |
| `--require-checks` | with `--replay`, fails if the replay has no checks |
| `--print-state` | prints the full state at each check |
| `--script <file>` | runs console commands at given frames, one `<frame> <command>` per line (e.g. `60 sim.hash` prints the state hash and seed) |
| `--server` | runs in real time at the fixed step until stopped, as a dedicated server |

Exit codes: `0` success (and matched), `1` failed to start, `2` diverged. The last line of output is `[HEADLESS] state <hash> frames <n> ...`, which is easy to parse from a script.

`--server` runs the same loop as everywhere else, except that it sleeps until the next fixed step instead of running as fast as it can.

## Null graphics backend

`RendererAPI::API::Null` (`renderer/rhi/backends/null`) implements the whole RHI without drawing anything. Meshes keep their geometry, textures their specs, materials their parameters, and framebuffers have a size and read back as black. The rest of the frame still runs (extraction, culling, pass setup, draw lists), so the CPU side of the renderer is exercised in headless runs and in `tests/rendering/null_rhi_tests.cpp` without a GPU.

This backend has no separate graphics thread. Everything runs on the calling thread.

## Determinism

`EngineCreationSettings::deterministic` makes runs reproducible. It is always on in `ShardHeadless`, and in the player with `--record` or `--replay`.

| What | How |
|---|---|
| Fixed timestep | Each frame is exactly one fixed step and the clock is never read. One `Run` is one physics step, and `GetDeltaTime()` returns the fixed step. |
| Stable system order | The scheduler runs systems within a phase in registration order, whatever the thread count (see `scheduler.hpp`). Scripts and physics bodies are kept in ordered maps, so they update in insertion order and not hash table order. |
| Seeded RNG | All random streams derive from one seed (`--seed`, saved in the replay). `GetEngine().MakeRandomStream("name")` gives a system its own PCG32 generator (integer-only, so identical on every platform). A given name always yields the same sequence for a given seed, whatever order systems start in. GUIDs are generated from the seed instead of the OS. Don't use `rand()`, `std::random_device` or `std::uniform_*_distribution` in anything that affects the simulation, since implementations differ between standard libraries. |
| Jolt deterministic mode | Jolt is built with `CROSS_PLATFORM_DETERMINISTIC` (CMake option `SHARD_DETERMINISTIC_PHYSICS`, on by default), which disables FMA and uses its own `sin`/`cos`. We also pass `-ffp-contract=off` so the compiler doesn't fuse `a * b + c` on its own. Physics runs on one thread in deterministic mode: Jolt's results don't depend on thread count, but the order of contact callbacks does, and so does whatever scripts do in them. |
| No outside interference | Hot reload is disabled. There is no window, so no resize or focus events unless the replay contains them. |

A game that wants replay support has to follow the same rules: use `MakeRandomStream` for randomness, `GetDeltaTime()` or the fixed step for time, and avoid `sin`, `cos`, `pow` and similar on anything that ends up in the state (the last bit can differ between C libraries). `Core::Random` uses none of them (`OnUnitSphere` and `OnUnitCircle` use rejection sampling).

### State hash

`HashSimulationState` (`src/engine/simulation_state.hpp`) reduces the simulation state to 64 bits. For each world it hashes every actor's transform, the pose and velocities of every physics body (as stored by Jolt), and every reflected field of every script, read-only ones included since they hold runtime state. Everything is visited by id, never in hash table order, and floats are hashed by their raw bits (`Determinism::StateHasher`), so one flipped bit changes the result. The hash has three parts (transforms, physics, scripts), so when a run diverges you know where to look.

## Replays

A replay (`platform/replay/input_replay.hpp`) is a text file holding the seed, fixed step, world, per-frame inputs and state checks ("after 60 frames the hash should be X").

```
shard-replay 1
seed 1
fixed-step #3c888889
world determinism.world
frames 600
check 60 bf7eabc5c5ff73f9 440e665432c876d0 c05a9388c8b98a39 890e766995745921
@20 key-down W
@120 key-up W
@400 mouse-move #43c80000 #42c80000 #40000000 #00000000
```

Keys are stored by name, so reordering the enum doesn't break old replays. Floats are stored as bit patterns and read back exactly. Replays can be edited by hand: decimal values work, and `#` starts a comment. Events for frame `F` are delivered just before frame `F` runs, so that frame's scripts see them.

To record, run `ShardGame --record run.replay`. It saves keyboard, mouse and gamepad input, plus a state check every `--check-every` frames (60 by default). The run is deterministic while recording.

To play it back, use `ShardGame --replay run.replay` to watch it (keyboard input is ignored), or `ShardHeadless --replay run.replay` to check it without a window. Both give the same hashes, because the graphics backend and the window don't affect the simulation.

## Regression test

`tests/determinism` holds a small project (a floor, a wall, some bodies that fall and get kicked at random by `RandomKicker` scripts, and a keyboard-controlled `Character`) and a replay (`golden/determinism.replay`) that walks around with WASD for 600 frames. CTest runs it through `ShardHeadless` as `Determinism.Replay`. CI runs it on Windows and Linux on runners without a GPU, and fails if any check doesn't match:

```
[REPLAY FAILED] the run diverged from the replay after 60 frames : expected state bf7eabc5c5ff73f9, got 5e47b74a8a3edcc4 (differs : transforms)
```

If a change is meant to affect the simulation (different physics settings, a new script field...), re-record the replay and commit it:

```
ShardHeadless --game build/plugins/game/bin/windows-x86_64/libGameModule.dll --project tests/determinism/project/determinism_project.json \
              --replay tests/determinism/golden/determinism.replay --write-replay tests/determinism/golden/determinism.replay
```

If it passes on one platform and fails on the other, something in the simulation depends on the platform: a `sin`/`cos` in the state, iteration over a hash table, uninitialized memory, a clock read. The three parts of the hash show which state to look at.

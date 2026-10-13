# Headless & determinism

The simulation of a project can run **without a window, a GPU or a sound card**, and it can run **the same way twice** : same inputs, same seed, same state, frame after frame, on Windows and on Linux. Both together make three things possible :

* a **dedicated server** : the game's simulation, with nothing to draw ;
* **simulation regression tests** in the CI : a recorded run is played back and every state it passes through is checked ;
* **replays** : the inputs of a session are a few KB, and are enough to see it again.

```
ShardHeadless --game libGameModule.dll --project example_project.json --replay run.replay
```

## ShardHeadless

`ShardHeadless` (`src/apps/headless`) is the player without the screen : the same engine, the same game module, the same simulation code. It boots on the **headless platform** (a window that only has a size and an event queue : SDL is never started) and the **null graphics backend**, and runs the frames.

| Option | |
|---|---|
| `--game <module>` `--project <file>` | the game module and the project (required) |
| `--world <path>` | world to load (default : the replay's, then the project's first) |
| `--frames <n>` | frames to run (default : the length of the replay, else 600) |
| `--seed <n\|random>` | seed of the random streams (default : the replay's, else 1) |
| `--replay <file>` | play a replay back : its inputs are fed to the game, its state checks are verified |
| `--write-replay <file>` | write the replay of this run : creates one, or re-records the checks of one after a change that is meant to alter the simulation |
| `--check-every <n>` | a state check every `n` frames (default 60) |
| `--expect <hash>` | the state after the last frame has to hash to that value |
| `--require-checks` | with `--replay` : fail if there is nothing to verify |
| `--print-state` | print the state at every check |
| `--script <file>` | console commands at given frames, one `<frame> <command>` per line (`60 sim.hash` prints the state hash and the seed) |
| `--server` | pace the run to the fixed step (real time), until stopped : a dedicated server |

Exit code : `0` ran (and matched), `1` could not start, `2` diverged. The last line is `[HEADLESS] state <hash> frames <n> ...` : one line a script can read.

A dedicated server is `ShardHeadless --server` : the loop is the same as everywhere else, it only sleeps until the next fixed step is due instead of running as fast as it can.

## The null graphics backend

`RendererAPI::API::Null` (`renderer/rhi/backends/null`) implements the whole RHI : meshes keep their geometry, textures their specifications, materials every parameter they are given, framebuffers have a size and read back black. **Nothing is drawn, but the whole frame runs** : extraction, culling, pass setup, draw lists. The CPU side of the renderer is therefore exercised by headless runs and by the tests (`tests/rendering/null_rhi_tests.cpp`), without a GPU.

There is no graphics thread with it : everything runs on the calling thread.

## Determinism

`EngineCreationSettings::deterministic` (always on in `ShardHeadless`, and in the player with `--record` / `--replay`) asks for a run that can be repeated :

| What | How |
|---|---|
| **Fixed step everywhere** | every frame lasts exactly one fixed step of the project (the clock is not read) : one `Run` = one physics step. Scripts see `GetDeltaTime() == fixedStep`. |
| **Stable system order** | the scheduler runs the systems of a phase in registration order, whatever the number of threads (see `scheduler.hpp`). The scripts of a world and the physics bodies are kept in ordered maps : they update in the order they were added, not in the order of a hash table. |
| **Seeded RNG in every system** | every random stream derives from one seed (`--seed`, written in the replay) : `GetEngine().MakeRandomStream("name")` gives a system a generator of its own (PCG32 : integers only, the same numbers everywhere). The same name gives the same sequence in every run with that seed, **whatever order the systems start in**. GUIDs are counted from the seed instead of drawn from the OS. `rand()`, `std::random_device` and `std::uniform_*_distribution` (which differs between standard libraries) must not be used by anything that is part of the simulation. |
| **Jolt deterministic mode** | Jolt is built with `CROSS_PLATFORM_DETERMINISTIC` (CMake option `SHARD_DETERMINISTIC_PHYSICS`, on by default : no fused multiply-add, its own `sin`/`cos`), and the compiler is told not to fuse `a * b + c` on its own (`-ffp-contract=off`). A deterministic run steps physics on **one thread** : Jolt gives the same result on any number of threads, but the order of the contact callbacks (and so of what the scripts do in them) depends on which job ran first. |
| **Nothing behind its back** | hot reload is off ; no window, so no resize or focus events unless the replay says so. |

A game that wants to stay replayable has to follow the same rules : take its randomness from `MakeRandomStream`, use `GetDeltaTime()` / the fixed step for time, and avoid `sin` / `cos` / `pow` and friends on values that end up in the state (the C library's last bit differs from one platform to another) : `Core::Random` itself has no such call (`OnUnitSphere` and `OnUnitCircle` use rejection sampling).

### The state hash

`HashSimulationState` (`src/engine/simulation_state.hpp`) condenses the state of the simulation into 64 bits : per world, the position / rotation / scale of every actor, the pose and velocities of every physics body (as Jolt has them), and **every reflected field of every script**, the read-only ones included (those are the runtime state). Everything is visited by id, never in the order of a hash table, and the floats go in as their IEEE bits (`Determinism::StateHasher`), so one flipped bit anywhere changes the hash. It is split in three parts (*transforms*, *physics*, *scripts*) : when a run diverges, the part that differs says where to look.

## Replays

A replay (`platform/replay/input_replay.hpp`) is a text file : the seed, the fixed step, the world, the inputs frame by frame, and **state checks** : "after 60 frames the state hashes to X".

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

Keys go by name (a replay survives the enum being reordered), floats by bit pattern (a replay reads back exactly what it recorded) ; it is writable by hand (decimals are accepted, `#` starts a comment). An event of frame `F` is delivered just before frame `F` runs, so the scripts of frame `F` see it.

**Recording** : `ShardGame --record run.replay` writes the keyboard, mouse and gamepad of a session, and a state check every `--check-every` frames (60). The run is deterministic while it is recorded (every frame is one fixed step). **Playing** : `ShardGame --replay run.replay` shows it again (the keyboard is ignored), `ShardHeadless --replay run.replay` checks it without a window. The same file, played by either, gives the same hashes : the graphics backend and the window do not take part in the simulation.

## The regression test

`tests/determinism` is a small project (a floor, a wall, bodies that fall and are kicked at random by `RandomKicker` scripts, a `Character` driven by the keyboard) and a replay of it (`golden/determinism.replay`) that walks around with W A S D for 600 frames. CTest runs it (`Determinism.Replay`) with `ShardHeadless` : the CI does, on **Windows and on Linux, on runners without a GPU**, and it fails when the hash of any check differs from what the replay says :

```
[REPLAY FAILED] the run diverged from the replay after 60 frames : expected state bf7eabc5c5ff73f9, got 5e47b74a8a3edcc4 (differs : transforms)
```

When a change is **meant** to alter the simulation (a different physics setting, a new script field), record the replay again and commit it :

```
ShardHeadless --game build/libGameModule.dll --project tests/determinism/project/determinism_project.json \
              --replay tests/determinism/golden/determinism.replay --write-replay tests/determinism/golden/determinism.replay
```

When it fails on one platform and passes on the other, something in the simulation depends on the platform : a `sin` / `cos` in the state, an iteration over a hash table, uninitialized memory, a clock. The three parts of the check say which state to look at.

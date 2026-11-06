# Getting started

Build the engine, open the example project in the editor, and run the tests.

## Requirements

- CMake 3.28.2 or later.
- A C++17 compiler : GCC or Clang on Linux (with `make` or `ninja`), GCC MinGW recommended on Windows.
- git, to clone the submodules.

```bash
git clone --recurse-submodules https://github.com/Windokk/Shard.git
cd Shard
# already cloned without them ?  git submodule update --init --recursive
```

## The editor's fonts

Place these two fonts in `resources/editor_resources/fonts/` (on both platforms) :

- [OpenSans-Regular.ttf](https://github.com/googlefonts/opensans)
- [lucide.ttf](https://unpkg.com/lucide-static@latest/font/lucide.ttf)

## Build and run

=== "Linux"

    Install what SDL3 needs to build its X11 / Wayland / audio / input backends, plus zlib and liburing (optional, faster async IO) :

    ```bash
    bash scripts/install_linux_deps.sh        # Debian / Ubuntu
    ```

    On another distribution install the equivalent packages : see the [SDL3 Linux notes](https://wiki.libsdl.org/SDL3/README-linux).

    ```bash
    ./build.sh              # add Release for an optimized build
    SHARD_NO_RUN=1 ./build.sh   # build only, do not start the editor
    ```

    `build.sh` makes imgui use the engine's `vulkan.h`, configures, builds everything and starts the editor on the example project.

=== "Windows"

    From Git Bash or MSYS2, make the imgui submodule use the engine's `vulkan.h` :

    ```bash
    bash scripts/patch_imgui.sh
    ```

    Then run `build.bat` (or `build.sh`). It compiles everything from the root : submodules, the engine, the editor and the game module. `SDL3.dll` is built with the project and lands in the build folder.

    Copy the `resources/engine_resources` and `resources/editor_resources` folders inside the build directory.

    To start the editor by hand on the example project, with OpenGL core as the rendering API :

    ```bash
    ./ShardEditor.exe --game plugins/game/bin/windows-x86_64/libGameModule.dll --project ../example_project/example_project.json --api opengl
    ```

The reflection files (`*.reflection.hpp`) are committed : `ShardReflect` only has to be run, with `scripts/regen_reflection.sh`, after changing a reflected class.

## Run the tests

The tests are built with the project :

```bash
ctest --test-dir build --output-on-failure
```

On Linux, `bash scripts/smoke_test.sh` (after a build) starts the player on the example project, loads its Cornell box world, renders a few frames and shuts down : it fails on a crash, a hang or an error in the log.

## No window, no GPU

`ShardHeadless` runs the simulation with no window, GPU or sound card, and can play back a recorded run and check that the state is the same on every frame :

```bash
ShardHeadless --game <module> --project <project.json> --replay run.replay
```

See [Headless mode and determinism](concepts/headless.md).

## What next

- Write your first scripts : [Write a game with the SDK](tutorials/write-a-game-with-the-sdk.md).
- Understand how the pieces fit : [Concepts](concepts.md).
- Look at what the editor does : [Editor](concepts/executables/editor.md).

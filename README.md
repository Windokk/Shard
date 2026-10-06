<h1 align="center">Shard Engine</h1>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset=".github/ShardLogoWhite.png" width="15%">
    <source media="(prefers-color-scheme: light)" srcset=".github/ShardLogoBlack.png" width="15%">
    <img alt="Fallback image description" src="default-image.png" width="20%">
  </picture>
</p>

<p align="center">
  Yet another game engine<br>
</p>

<!-- BIG-REFACTO:BEGIN - temporary section, delete it (up to BIG-REFACTO:END) when the branch is merged -->
## [Temporary] `big-refacto` branch roadmap

Goal: make the code follow the architecture board (layers depend downwards only, peers are decoupled).
**No new feature in this branch** - only restructuring of what already exists.

### Method: decouple, then lock
A module becomes its own CMake target only once it has no dependency upwards. Each step is therefore:
decouple, extract the target, then lock it with `scripts/check_deps.py` in CI (a baseline file that can only go down).

Done when: the violation count goes down, the build and the tests pass, and the editor opens `example_project`.

### Current state (frozen in `scripts/deps_baseline.json`, lowered after each step)
Layers, bottom to top: `core` < `platform` < `assets` < `world` < {`renderer`, `physics`, `audio`, `input`} < `genres` < `apps`.
Run `python scripts/check_deps.py --matrix` for the full picture. Current violations: 8 module pairs, 52 includes (started at 13 pairs, 66 includes).

| Violation | Includes | Cause |
|---|---|---|
| `world` -> renderer | 23 | `Actor` knows Light/Probe/Camera; `Level` and `Skybox` call the renderer |
| `assets` -> renderer 12, world 6, audio 1 | 19 | `resources_manager` (mesh, shader, material, pipeline, envmap), `material_serializer`, serializers include `world/engine.hpp` |
| `physics` -> renderer 2, `audio` -> renderer 2 (peers) | 4 | `physics_body` (`mesh.hpp`, `debug_shapes`), `audio_manager` (`renderer.hpp`, `camera_manager`) |
| `world` -> audio 4, physics 2 | 6 | `Actor`, `Level`, `level_asset_prefetcher` |
| `gl*()` calls outside the backends | 1 file | `apps/editor/gui/main_window.cpp` (`glClearColor`, `glClear`) |

Not measured yet: dependencies inside `renderer/` (60% of the engine code).

### Steps
- [x] **0 - Safeguards**: `scripts/check_deps.py` + baseline + CI job (`.github/workflows/deps.yml`); folder structure finished (reflection, `Object`/`engine.cpp`, `FileManager` -> `assets/vfs/`, projects, events, time, diagnostics, `keys.hpp` -> `platform/`); the duplicated GLFW code of editor/player is merged into `engine/platform/glfw/` (`GLFWInput`, `GLFWWindowBase`, `GLFWPlatform<WindowT>`). Side effect: the player now maps the Enter key (it was missing from its copy).
- [x] **1 - Foundation & platform** (`ShardCore`, `ShardPlatform`): `core` no longer depends on anything and `platform` only on `core`. `COL_RGB` -> `core/color.hpp`; `logger.cpp` no longer includes the engine; the profiler knows no module: stats are filled by providers that each module registers (renderer, audio, engine) and the scoped samples use `Profiler::Active()`. Separate CMake targets `ShardCore` and `ShardPlatform` (linked by `Engine`). Checked: build, 61 tests, editor and player start on a scratch copy of `cornell.lvl`.
- [ ] **2 - Assets** (`ShardAssets`, the main knot): `resources_manager` becomes a generic handle cache with a per-type loader registry; `material_serializer` moves to `renderer/material/`; `Mesh` split into CPU `MeshData` (assets) and GPU resource (rhi).
- [ ] **3 - World** (`ShardWorld`, removes `world` -> renderer/audio/physics): `Actor` only knows `Component` + the registry; the renderer reads the world (`RenderScene::Collect(const Level&)`, no snapshot yet); `Skybox` becomes a renderer component; world -> audio/physics through the registry (asset-dependency visitor via reflection); event types live in the module that defines them; frame loop of `engine.cpp` split into named stages with no behavior change.
- [ ] **4 - Peers** (`ShardPhysics`, `ShardAudio`): audio listener comes from a world component; `debug_shapes` becomes a core debug-draw interface implemented by `renderer/features/debug`.
- [ ] **5 - Renderer** (`ShardRHI`, `ShardRenderer`): measure internal deps first (target order `rhi` < `material` < `features` < `frontend`); move the two `gl*` calls out of `main_window.cpp`.
- [ ] **6 - Final lock**: `Engine` becomes an `INTERFACE` target grouping the others; merge `game_module_loader.hpp` and `editor_module_loader.hpp` into the engine; baseline at zero.

### Out of scope (separate branches)
ECS with archetypes and system graph, Extract snapshot + render thread, new backends (Vulkan, D3D12, Null/headless), render graph, bindless, job system, `apps/headless`.
<!-- BIG-REFACTO:END -->

## Screenshots

<div align="center">
  <img src=".github/ScreenShot0.jpg"/>
  <p><i>Path traced render</i></p>
  <img src=".github/ScreenShot1.png"/>
  <p><i>View in engine</i></p>
  <img src=".github/Screenshot2.png"/>
</div>

## How to use (Linux)

// WIP

## How to use (Windows)

### Build tools :
- CMake 3.28.2 or later
- C++ 17 Compiler (GCC MinGW recommended)

### Proprietary Dependencies :
- FMOD Core API 2.03.14

### Editor Fonts : (Place both in resources/editor_resources/fonts/)
- [OpenSans-Regular.ttf](https://github.com/googlefonts/opensans)
- [lucide.ttf](https://unpkg.com/lucide-static@latest/font/lucide.ttf)


### How to build & run :

Modify imgui submodule to use our vulkan.h (src/engine/renderer/rhi/backends/glad/include/glad/vulkan.h)

Place the fonts (.ttf) inside their folder (resources/editor_resources/fonts/)

Run build.bat or build.sh (depending on your OS)

This will compile everything from root : submodules, the engine, the editor, and the game module (loaded with the game app)

Drop fmod.dll and glfw3.dll inside the build folder

Copy the resources/engine_resources and resources/editor_resources folders inside the build directory

Drop ShardReflect executable inside build/tools/

This runs the editor, loads the game module, opens the project at "project path" and uses open gl core as the rendering api
```bash
./ShardEditor.exe --game libGameModule.dll --project ..\\example_project\\example_project.json --api opengl
```

## Credits/Dependencies

- Libraries/Projects :
  - [GLFW](https://github.com/glfw/glfw)
  - [GLM](https://github.com/g-truc/glm)
  - [Jolt](https://github.com/jrouwe/JoltPhysics)
  - [IconFontCppHeaders](https://github.com/juliettef/IconFontCppHeaders)
  - [ImGui](https://github.com/ocornut/imgui)
  - [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo)
  - [freetype](https://github.com/freetype/freetype)
  - [glad](https://github.com/Dav1dde/glad)
  - [ufbx](https://github.com/ufbx/ufbx)
  - [json for c++](https://github.com/nlohmann/json)
  - [fmod](https://www.fmod.com/)
  - [ImOGuizmo](https://github.com/fknfilewalker/imoguizmo)
  - [ImGuiNotify](https://github.com/TyomaVader/ImGuiNotify/tree/Dev)

- Fonts :
  - [Lucide icons](https://lucide.dev/)
  - [Open Sans](https://fonts.google.com/specimen/Open+Sans)

- Sounds/Music :
  - [TownTheme.mp3](https://opengameart.org/content/town-theme-rpg)

- Models :
  - "Rubik's Cube" (<https://skfb.ly/6U7pp>) by RED2000 is licensed under Creative Commons Attribution (<http://creativecommons.org/licenses/by/4.0/>).
  - Intel Sponza 2022 Scene commissioned by Frank Meinl, sponsored by Anton Kaplanyan [Link](https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-processing-research/samples.html)
  - "Cerberus" Gun model [Andrew Maximov](https://artisaverb.info/PBT.html) 
  - "Stanford Dragon (Vrip)" (<https://skfb.ly/BSZn>) by 3D graphics 101 is licensed under Creative Commons Attribution-NonCommercial (<http://creativecommons.org/licenses/by-nc/4.0/>).

- Textures :
  - [Qwantani Afternoon (Pure Sky)](https://polyhaven.com/a/qwantani_afternoon_puresky)

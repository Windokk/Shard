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
Run `python scripts/check_deps.py --matrix` for the full picture. Current violations: 2 module pairs, 5 includes (started at 13 pairs, 66 includes).

| Violation | Includes | Cause |
|---|---|---|
| `physics` -> renderer 3, `audio` -> renderer 2 (peers) | 5 | `physics_body` (`mesh.hpp`, `renderer.hpp`, `debug_shapes`), `audio_manager` (`renderer.hpp`, `camera_manager`) |
| `gl*()` calls outside the backends | 1 file | `apps/editor/gui/main_window.cpp` (`glClearColor`, `glClear`) |

Not measured yet: dependencies inside `renderer/` (60% of the engine code).

### Steps
- [x] **0 - Safeguards**: `scripts/check_deps.py` + baseline + CI job (`.github/workflows/deps.yml`); folder structure finished (reflection, `Object`/`engine.cpp`, `FileManager` -> `assets/vfs/`, projects, events, time, diagnostics, `keys.hpp` -> `platform/`); the duplicated GLFW code of editor/player is merged into `engine/platform/glfw/` (`GLFWInput`, `GLFWWindowBase`, `GLFWPlatform<WindowT>`). Side effect: the player now maps the Enter key (it was missing from its copy).
- [x] **1 - Foundation & platform** (`ShardCore`, `ShardPlatform`): `core` no longer depends on anything and `platform` only on `core`. `COL_RGB` -> `core/color.hpp`; `logger.cpp` no longer includes the engine; the profiler knows no module: stats are filled by providers that each module registers (renderer, audio, engine) and the scoped samples use `Profiler::Active()`. Separate CMake targets `ShardCore` and `ShardPlatform` (linked by `Engine`). Checked: build, 61 tests, editor and player start on a scratch copy of `cornell.world`.
- [x] **2 - Assets** (`ShardAssets`): `assets` depends on nothing above it. `ResourcesManager` is a generic cache: the module that owns a type registers it with `RegisterKind(AssetKind, AssetKindInfo)` (loader, AssetID setter, dependencies, eviction rules) and call sites use `Get<T>(AssetKind, path)`, `Has`, `Adopt`, `Unload`. The renderer registers mesh/texture/envmap/shader/compute/material/probe bake (`renderer/frontend/render_asset_kinds.cpp`), audio the sound, world the world. `material_serializer` -> `renderer/material/` (the JSON reader `PeekMaterialAssetRefs` stays in `assets/`, it has no renderer type). The managers `assets` needs (`AssetIDManager`, `FileManager`) are injected instead of fetched from the `Engine` singleton. Checked: build, 79 tests (18 new for the resources manager), editor and player start. The `Mesh` CPU/GPU split moved to step 4, it is what decouples `physics`.
- [x] **3 - World** (`ShardWorld`): `world` depends on `assets` and below only. What a module adds to the world is declared by the module: `World` keeps no renderer/physics/audio type, each module hangs its own data on it through a world extension (`world.Ext<RenderWorldData>().lights`, `Ext<PhysicsWorldData>().bodies`, `Ext<AudioWorldData>().sources`) that is told when a component joins/leaves the world, on load, on play and when the world settings are (de)serialized (`world/world_extension.hpp`). `Actor` only knows `Component` and the registry: engine components ("model", "light", "camera", "probeVolume", "physics_body", "audio") are registered with `RegisterBuiltinComponent<T>(name, assetRefs)` by `RegisterRenderingModule()` / `RegisterPhysicsModule()` / `RegisterAudioModule()` (called by the engine), and `Actor::AddComponentByName` builds them. `Transform` no longer knows lights/models/volumes: it calls `Component::OnTransformChanged(flags)` on its actor's other components. `Skybox` and its world settings moved to the renderer (`renderer/frontend/skybox.*`, `RenderWorldData::SetSkybox`). The world asset prefetcher knows no asset type: `ResourcesManager::PlanPrefetch` walks the owners' dependencies and each kind says how to decode itself on a worker (`AssetKindInfo::prefetch`: mesh, texture, probe bake, sound). What a world file references is collected through the registered components/settings (`CollectWorldAssetRefs`), not hardcoded. Contact events moved to `physics/physics_events.hpp` and `PhysicsManager` calls the scripts directly. `engine.cpp` (the composition root: it creates every manager) left `world/` for `src/engine/`, its frame loop is split into named stages (`UpdateSimulation`, `UpdateWorlds`, `SyncPhysicsBodies`, `RenderFrame`, `PollInput`, `Present`). Checked: build, 79 tests, editor and player start on a scratch copy of `example_project` (sponza + baked probes). Deviations: no `RenderScene::Collect` (the renderer reads `RenderWorldData` directly, as it read `World` before), and `ProbeVolume` is activated with the other renderer data on load (before the scripts' `OnWorldLoaded`, it used to be after). The `physics` -> renderer edge went from 2 to 3 includes: `physics_body.cpp` always needed `renderer.hpp`, it only got it through `actor.hpp` before (step 4).
- [ ] **4 - Peers** (`ShardPhysics`, `ShardAudio`): audio listener comes from a world component; `Mesh` split into CPU `MeshData` (assets) and GPU resource (rhi) so `physics_body` stops including `mesh.hpp`; `debug_shapes` becomes a core debug-draw interface implemented by `renderer/features/debug`.
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

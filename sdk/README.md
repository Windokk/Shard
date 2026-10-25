# Shard SDK

What a plugin built outside the engine needs : the headers of the C ABI, a header-only C++ layer over it, a CMake package and a template. A plugin built with it links nothing of the engine and loads in every engine of the same ABI major (and a minor at least its own), whatever compiler built either side.

```
sdk/
  VERSION                   the SDK's version = the C ABI's revision (major.minor.patch)
  include/shard/            the C ABI : C11 and C++17, plain C
  include/shard/cpp/        the C++ SDK : header-only C++17, compiled into the plugin
  cmake/                    find_package(ShardSDK) and shard_sdk_add_plugin
  bin/ShardPkg              packs and signs a plugin (installed SDK only : the engine's build makes it)
  templates/plugin/         a plugin to start from
```

In short : a plugin and the engine cannot share C++ objects, so they talk through plain C functions (the **C ABI**, `include/shard/`). Writing those calls by hand is long, so the **C++ SDK** (`include/shard/cpp/`) wraps them in classes and lambdas, and is compiled into your plugin. It adds no run-time cost to a simple loop ; the few things that can slow a plugin down are listed in `docs/docs/concepts/plugins.md` ("Does the SDK make a plugin slower ?"). Use the C++ SDK by default, and the C headers directly only for a hot spot or from a language other than C++.

Get it with `cmake --install build --component sdk --prefix <dir>` (it lands in `<dir>/sdk`) or the `ShardSDKArchive` target (`build/shard-sdk-<version>.zip`). This folder of the repository is an SDK too. Start with `docs/docs/tutorials/write-a-game-with-the-sdk.md` ("Write a game with the SDK") ; the reference is `docs/docs/api_reference.md` (generated from these headers), the concepts are in `docs/docs/concepts/plugins.md` ("The C ABI and the SDK"), and the plan and its decisions in `docs/design-notes/c-abi-sdk.fr.md`.

## The C ABI

| Header | Content |
|---|---|
| `shard.h` | every header below |
| `shard_abi.h` | versions (`SHARD_ABI_MAJOR/MINOR`), `ShardResult`, `ShardStr`, `ShardPluginCtx`, `ShardState`, log levels |
| `shard_host.h` | `ShardHostApi` (what the host gives), `ShardPluginInfo` and `ShardPluginCallbacks` (what the plugin returns), `shard_plugin_entry` |
| `shard_plugin.h` | `shard.plugin` 1.1 : the plugin's id, version, folder, host, permissions |
| `shard_cvars.h` | `shard.cvars` 1.0 : variables the plugin owns (bool, int, float, string) |
| `shard_commands.h` | `shard.commands` 1.1 : console commands ; running a console line |
| `shard_components.h` | `shard.components` 1.4 : component types described field by field (an ECS component and an actor component at once) ; blobs, text and asset fields ; in a script, references to other actors and strings the plugin owns ; `on_added` / `on_removed` / `on_changed` |
| `shard_ecs.h` | `shard.ecs` 1.1 : queries, component access and structural changes, from a system ; the engine's components it can read ; moving an actor |
| `shard_systems.h` | `shard.systems` 1.0 : systems in the scheduler's phases |
| `shard_events.h` | `shard.events` 1.1 : events by name |
| `shard_hooks.h` | `shard.hooks` 1.0 : the engine's hooks, by name |
| `shard_services.h` | `shard.services` 1.0 : tables of functions plugins offer each other |
| `shard_extensions.h` | `shard.extensions` 1.0 : asset validators, asset types, asset importers |
| `shard_content.h` | `shard.content` 1.0 : the modules' content (`mod://`, logical paths) |
| `shard_state.h` | `shard.state` 1.0 : what survives a hot reload |
| `shard_actors.h` | `shard.actors` 1.2 : the actors of a world : pose, hierarchy, tags, creating and finding, the engine's components as JSON ; stable ids, world bounds, finding in a sphere or a box |
| `shard_scripts.h` | `shard.scripts` 1.2 : a plugin's classes living on actors, called back as the engine's own scripts are ; physics contacts ; running in the editor's edit mode, told of the fields the inspector edits |
| `shard_input.h` | `shard.input` 1.1 : keyboard, mouse and gamepads, polled ; the events of the frame, modifiers, clipboard, cursor shape, text input, capturing the input for a UI |
| `shard_physics.h` | `shard.physics` 1.1 : velocities, forces, impulses, gravity and ray casts ; every hit of a ray, overlaps, sweeps, static colliders without an actor (voxel chunks, navigation obstacles) |
| `shard_audio.h` | `shard.audio` 1.0 : playing the sound of an actor, one shot sounds |
| `shard_worlds.h` | `shard.worlds` 1.1 : loading a world, spawning a prefab by name ; the loaded worlds |
| `shard_editor.h` | `shard.editor` 1.2 : panels, menu items, toolbar buttons, inspectors, viewport gizmos and the selection of the editor, drawn with a set of widgets (`ShardUiApi`, `ShardGizmoApi`) ; a drawing canvas (lines, shapes, text, images, mouse and keys) for node graphs, undo entries, asset openers and templates. Offered by the editor only |
| `shard_jobs.h` | `shard.jobs` 1.0 : the engine's worker threads : jobs, parallel loops, handing a result back to the main thread |
| `shard_time.h` | `shard.time` 1.0 : the frame, fixed step and simulation time, the time scale, a hash of the simulation state (to check a replay or a network peer) |
| `shard_assets.h` | `shard.assets` 1.0 : the project's assets by path or id : listing, reading and writing files, reading the triangles of a mesh and the pixels of an image |
| `shard_reflect.h` | `shard.reflect` 1.0 : the component and event types with their fields, and functions plugins register and call by name (the blocks of a visual script) |
| `shard_net.h` | `shard.net` 1.0 : UDP and TCP sockets, polled at the start of the frame. A plugin declares the permission `net` |
| `shard_render.h` | `shard.render` 1.0 : meshes, textures and materials made by the plugin, objects drawn in the world, instanced and sprite batches (particles), skinned meshes, a 2D overlay over the viewport (a UI), the camera |
| `shard_debug.h` | `shard.debug` 1.0 : lines, shapes and texts drawn for a duration (paths, volumes, forces) |
| `shard_experimental.h` | `shard.experimental.world_ecs` 0.1 : the engine's entities from the main thread. No compatibility promise |

A plugin sets `"abi": "c1"` in its `shard-plugin.json` and exports `shard_plugin_entry` (`shard_sdk_add_plugin` and `SHARD_C_PLUGIN` do both). In the engine's repository, `tests/plugin_fixtures/c_plugin/c_plugin.c` is a plugin in C that uses every interface, `tests/plugin_fixtures/sdk_spin/spin.cpp` one in C++, and `tests/plugin_fixtures/game_scripts` a game of several files with the gameplay layer.

## The C++ SDK

`#include <shard/game.hpp>` and write your game as classes : `Shard::Script` for what lives on an actor (`OnPlay`, `OnTick(dt)`...), `Shard::Actor` and `Shard::Scene` for the world, `Shard::Input` for the keyboard, mouse and pads, `Shard::Physics`, `Shard::Audio` and `Shard::Worlds` for rays, sounds and loading, `Shard::Random` for numbers a replay reproduces, `SHARD_EDITOR_PANEL` for a window in the editor, the maths of `shard/cpp/math.hpp`. The fields of a script are declared in its class, one by one, with `SHARD_FIELD(float, power, Range(0.f, 10.f), Tooltip("..")) = 3.f;` (`shard/cpp/field.hpp`), and the engine's components are typed handles (`actor.Add<Shard::Model>().SetMesh(mesh)`, `shard/cpp/engine_components.hpp`). Each source file registers its own scripts, components and systems with a line at its bottom (`SHARD_REGISTER_SCRIPT`, `SHARD_DEFINE_COMPONENT`, `SHARD_ON_LOAD`, see `shard/cpp/registry.hpp`), so a game is as many files as you like and `SHARD_GAME_MODULE()` in one of them. For a plugin that is not a game, derive from `Shard::Plugin` and end with `SHARD_C_PLUGIN(YourClass)`. The C++ layer uses the standard library freely : it is compiled into the plugin and nothing of it crosses the ABI. Every callback catches what the plugin throws.

## Revisions

| ABI | Added |
|---|---|
| 1.0 | the entry point, the callbacks, and the interfaces plugin, cvars, commands, components, ecs, systems, events, hooks, services, extensions, content, state |
| 1.1 | `shard.plugin` 1.1 : `host`, `has_permission` |
| 1.2 | `shard.components` 1.1 : blobs (`SHARD_FIELD_BLOB`, `blob_create` / `blob_data` / `blob_resize` / `blob_free`) ; `shard.events` 1.1 : `describe` ; `ShardHostApi::engineVersion` filled |
| 1.3 | `shard.components` 1.2 : text and asset fields (`SHARD_FIELD_STRING`, `SHARD_FIELD_ASSET`, `string_set` / `string_get`) ; `shard.ecs` 1.1 : `set_local_transform`, a system moves the actor behind an entity |
| 1.4 | `shard.scripts` 1.0, `shard.actors` 1.0, `shard.input` 1.0 : gameplay classes on actors, the world, the keyboard ; the C++ gameplay layer (`shard/game.hpp`) |
| 1.5 | `shard.physics` 1.0, `shard.audio` 1.0, `shard.worlds` 1.0, `shard.editor` 1.0 ; `shard.scripts` 1.1 : `on_contact_*`, `text_get` / `text_set` ; `shard.components` 1.3 : `SHARD_FIELD_ACTOR`, `SHARD_FIELD_ACCESSOR` ; `shard.plugin` 1.2 : `random_stream` |
| 1.6 | `shard.editor` 1.1 : many more widgets (tabs, tables, menus, popups, groups, disabled sections...), menu items, toolbar buttons, inspectors, gizmos over the viewport, the selection, notifications ; a sandboxed plugin may use it with the permission `editor` |
| 1.7 | the base for particle systems, AI and navigation, voxels, a runtime UI, animation, networking and visual scripting. New : `shard.jobs`, `shard.time`, `shard.assets`, `shard.reflect`, `shard.net`, `shard.render`, `shard.debug` 1.0. Grown : `shard.physics` 1.1 (queries, colliders), `shard.input` 1.1 (events, clipboard, cursor, capture), `shard.actors` 1.2 (stable ids, spatial queries), `shard.components` 1.4 (lifecycle callbacks), `shard.scripts` 1.2 (edit mode, field changes), `shard.worlds` 1.1, `shard.commands` 1.1 (`execute`), `shard.editor` 1.2 (canvas, undo, asset openers and templates). Limits : no texture arrays, a 2D clip mask is its bounding rectangle, sprites and overlays are drawn by OpenGL (bindless textures) and Vulkan alike, the overlay of a Vulkan frame repacking its geometries every frame. The sandbox (WebAssembly) does not offer `jobs`, `time`, `assets`, `reflect`, `net`, `render` and `debug` yet : native plugins only |

A copy of the 1.0 headers is frozen in `tests/plugin_fixtures/abi_1_0` : a plugin built against it must keep loading (`tests/plugins/abi_compat_tests.cpp`).

## Rules for changing these headers

* A struct that crosses the boundary starts with `uint32_t structSize` and only ever grows at its end. Tables grow at their end too.
* A change to a struct or a table raises `SHARD_ABI_MINOR`, the interface's minor in `query_interface` (`src/engine/plugins/abi/c_plugin.cpp`) and `VERSION` ; an incompatible change raises the major. Say it in the table of revisions above.
* `tests/plugins/abi_layout_tests.cpp` pins every layout : update it in the same change, deliberately.
* Fixed size types only inside a struct (`int32_t`, `uint64_t`), handles instead of pointers to host objects, no exception across the boundary, every function documents its thread (`any`, `system`, `main`).
* Never edit `tests/plugin_fixtures/abi_1_0`.
* `shard.experimental.*` interfaces promise nothing : major 0, handed out for their exact minor, refused to signed native plugins, not pinned by the layout tests and not counted in `SHARD_ABI_MINOR`. Something moves out of them into a frozen interface (and a new ABI minor) once it has been used enough.
* The comments are the API reference : run `python scripts/gen_api_reference.py` after changing a header (the test `Docs.ApiReference` checks it, and that the revision a header announces is the one the host offers).

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

Get it with `cmake --install build --component sdk --prefix <dir>` (it lands in `<dir>/sdk`) or the `ShardSDKArchive` target (`build/shard-sdk-<version>.zip`). This folder of the repository is an SDK too. Start with `docs/docs/tutorials.md` ("Write a plugin outside the engine repository") ; the reference is `docs/docs/api_reference.md` (generated from these headers), the concepts are in `docs/docs/concepts/plugins.md` ("The C ABI and the SDK"), and the plan and its decisions in `docs/design-notes/c-abi-sdk.fr.md`.

## The C ABI

| Header | Content |
|---|---|
| `shard.h` | every header below |
| `shard_abi.h` | versions (`SHARD_ABI_MAJOR/MINOR`), `ShardResult`, `ShardStr`, `ShardPluginCtx`, `ShardState`, log levels |
| `shard_host.h` | `ShardHostApi` (what the host gives), `ShardPluginInfo` and `ShardPluginCallbacks` (what the plugin returns), `shard_plugin_entry` |
| `shard_plugin.h` | `shard.plugin` 1.1 : the plugin's id, version, folder, host, permissions |
| `shard_cvars.h` | `shard.cvars` 1.0 : variables the plugin owns (bool, int, float, string) |
| `shard_commands.h` | `shard.commands` 1.0 : console commands |
| `shard_components.h` | `shard.components` 1.1 : component types described field by field (an ECS component and an actor component at once) |
| `shard_ecs.h` | `shard.ecs` 1.0 : queries, component access and structural changes, from a system ; the engine's components it can read |
| `shard_systems.h` | `shard.systems` 1.0 : systems in the scheduler's phases |
| `shard_events.h` | `shard.events` 1.1 : events by name |
| `shard_hooks.h` | `shard.hooks` 1.0 : the engine's hooks, by name |
| `shard_services.h` | `shard.services` 1.0 : tables of functions plugins offer each other |
| `shard_extensions.h` | `shard.extensions` 1.0 : asset validators, asset types, asset importers |
| `shard_content.h` | `shard.content` 1.0 : the modules' content (`mod://`, logical paths) |
| `shard_state.h` | `shard.state` 1.0 : what survives a hot reload |
| `shard_experimental.h` | `shard.experimental.world_ecs` 0.1 : the engine's entities from the main thread. No compatibility promise |

A plugin sets `"abi": "c1"` in its `shard-plugin.json` and exports `shard_plugin_entry` (`shard_sdk_add_plugin` and `SHARD_C_PLUGIN` do both). In the engine's repository, `tests/plugin_fixtures/c_plugin/c_plugin.c` is a plugin in C that uses every interface, and `tests/plugin_fixtures/sdk_spin/spin.cpp` one in C++.

## The C++ SDK

`#include <shard/cpp/plugin.hpp>`, derive from `shard::Plugin`, end with `SHARD_C_PLUGIN(YourClass)`. It uses the standard library freely : it is compiled into the plugin and nothing of it crosses the ABI. Every callback catches what the plugin throws.

## Revisions

| ABI | Added |
|---|---|
| 1.0 | the entry point, the callbacks, and the interfaces plugin, cvars, commands, components, ecs, systems, events, hooks, services, extensions, content, state |
| 1.1 | `shard.plugin` 1.1 : `host`, `has_permission` |
| 1.2 | `shard.components` 1.1 : blobs (`SHARD_FIELD_BLOB`, `blob_create` / `blob_data` / `blob_resize` / `blob_free`) ; `shard.events` 1.1 : `describe` ; `ShardHostApi::engineVersion` filled |

A copy of the 1.0 headers is frozen in `tests/plugin_fixtures/abi_1_0` : a plugin built against it must keep loading (`tests/plugins/abi_compat_tests.cpp`).

## Rules for changing these headers

* A struct that crosses the boundary starts with `uint32_t structSize` and only ever grows at its end. Tables grow at their end too.
* A change to a struct or a table raises `SHARD_ABI_MINOR`, the interface's minor in `query_interface` (`src/engine/plugins/abi/c_plugin.cpp`) and `VERSION` ; an incompatible change raises the major. Say it in the table of revisions above.
* `tests/plugins/abi_layout_tests.cpp` pins every layout : update it in the same change, deliberately.
* Fixed size types only inside a struct (`int32_t`, `uint64_t`), handles instead of pointers to host objects, no exception across the boundary, every function documents its thread (`any`, `system`, `main`).
* Never edit `tests/plugin_fixtures/abi_1_0`.
* `shard.experimental.*` interfaces promise nothing : major 0, handed out for their exact minor, refused to signed native plugins, not pinned by the layout tests and not counted in `SHARD_ABI_MINOR`. Something moves out of them into a frozen interface (and a new ABI minor) once it has been used enough.
* The comments are the API reference : run `python scripts/gen_api_reference.py` after changing a header (the test `Docs.ApiReference` checks it, and that the revision a header announces is the one the host offers).

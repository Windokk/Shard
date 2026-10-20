# Plugins and modules

How to extend Shard without changing the engine : game code, genre toolkits, mods, datapacks, editor tools, server add-ons.

Everything that extends Shard is a **plugin**. A **module** is a plugin that also carries game content (components, assets, worlds). The engine's own parts (physics, audio, renderer...) show up in the same list as modules, and the game is a module too, so a plugin can depend on any of them the same way.

The code lives in `src/engine/plugins/`. `Plugins::PluginManager` (`manager.hpp`) finds, checks, loads, runs and unloads the plugins of an executable.

## Kinds

| Kind | Loaded by | Typical use |
|---|---|---|
| `EnginePlugin` | the editor, the player, `ShardHeadless` | render features, importers, extensions of physics or audio, per-frame work |
| `EditorPlugin` | the editor only | windows, inspectors, editor commands |
| `ServerPlugin` | `ShardHeadless` | accepting or refusing clients, administration, metrics |
| `ToolPlugin` | the offline tools (`ShardAssetValidator`) | checks for the asset validator, tasks the tools run |
| `IModule` | wherever its manifest says | game code, genre modules, mods, datapacks |

`IModule` derives from `IPlugin`, so a module has everything a plugin has (manifest, lifecycle, dependencies, registration context) plus content : a file tree mounted as `mod://<id>/`, and callbacks that follow the worlds.

Module kinds :

| Module kind | Contains | Runs code |
|---|---|---|
| `engine` | a part of the engine itself (`shard.physics`, `shard.audio`...) | yes, compiled into the executable |
| `genre` | a reusable toolkit : voxel, planetary, racing... | yes, native |
| `code` | a game's own C++ | yes, native |
| `mod` | content and logic made by someone else | yes, native |
| `datapack` | worlds, prefabs, data tables, translations | no |

A plugin lists the hosts it can run in. A host only loads the plugins that name it, so editor code never ends up in a shipped player.

## Installing a plugin

A plugin is a folder with a `shard-plugin.json` at its root, or the same folder packed in a `.shardplugin` archive (a ZIP file : see [Packaging](#packaging-signing-and-registries)). Put it in the project's plugins folder (`pluginsFolder` in the project file, shown under *Project Settings > Plugins*), or in any folder listed in the `SHARD_PLUGIN_PATH` environment variable (separated by `:`, or `;` on Windows). Shard looks at that folder, and one level of subfolders below it, for plugin folders and `.shardplugin` archives. An archive is unpacked once into a cache folder (`.cache/plugin_cache` next to the project) named after its hash, then loaded from there.

The project file can then say which plugins it wants :

```json
"plugins": [
  { "id": "com.acme.weather", "version": "^1.4", "required": true,
    "settings": { "rain_rate": 0.4 } },
  { "id": "com.acme.clouds", "enabled": false },
  { "id": "my.local.mod", "path": "mods/local" }
]
```

| Key | Meaning |
|---|---|
| `id` | the plugin |
| `version` | a version range ; when several versions of the plugin are found, the highest one inside it is used, and a plugin that does not fit is refused |
| `enabled` | `false` leaves the plugin out |
| `required` | the project cannot run without it : if it cannot be loaded, startup stops with the reason |
| `path` | a plugin folder or a `.shardplugin` archive, relative to the project, in addition to the plugins folder |
| `allowUnsigned` | load this plugin even if it is a native plugin (tier 1) with no signature from a trusted publisher, see [Trust levels](#trust-levels) |
| `settings` | values for the plugin's console variables. A key is relative to the plugin's variables (`rain_rate` is `com.acme.weather.rain_rate`) |

The project file can also list `"registries"`, the places `plugin.search` and `plugin.install` look in (see below).

A plugin the project does not mention is loaded anyway if it sits in a plugins folder. List it only to configure it, turn it off, or require it.

On the first run Shard writes `plugins.lock` next to the project file : the exact version and a hash of the content of every plugin that was loaded. Commit it. On later runs a difference (another version, another content, a plugin that appeared or vanished) is written to the log, and `--strict-plugin-lock` makes it an error. `ShardHeadless` and the player are strict when they play a replay. Rewrite the lock with the *Write plugins.lock* button of the Plugins panel, or `plugin.lock write` in the console. A read-only run (`--frames`, a replay) never writes it.

### Command line

The editor, the player and `ShardHeadless` take these :

| Option | |
|---|---|
| `--plugin <path>` | a plugin folder, a `shard-plugin.json`, or a library ; repeatable |
| `--plugin-path <dir>` | a folder that holds plugins ; repeatable |
| `--disable-plugin <id>` | leave a plugin out of this run ; repeatable |
| `--hot-reload-plugins` | player : allow `plugin.reload` (the editor always allows it ; a deterministic run never does) |
| `--strict-plugin-lock` | a `plugins.lock` that does not match is an error |
| `--ignore-safe-mode` | load a plugin even if the last run crashed in it |
| `--registry <source>` | a registry for `plugin.search` and `plugin.install` : an `index.json`, a folder holding one, or an `http://` URL ; repeatable (`SHARD_PLUGIN_REGISTRY` adds one) |
| `--trust-file <path>` | a file of trusted publishers ; repeatable (default : `plugin-trust.json` next to the project) |
| `--allow-unsigned-plugins` | load native plugins and install from registries without a signature of a trusted publisher |

`--game <library>` still works : the library is loaded as the plugin `game`, a code module. It does not need a manifest. The engine's own game module is built with `shard_add_plugin` (id `game`), so it sits at `<build>/plugins/game/bin/<platform>/` with its `shard-plugin.json` : pass the library to `--game`, or the folder to `--plugin` to load it through its manifest.

`ShardHeadless` also has :

| Option | |
|---|---|
| `--list-plugins` | load the plugins, print them, exit |
| `--validate-plugin <folder>` | check the manifest and the content of a folder without loading anything ; exit code 1 on problems |
| `--check-plugins` | run the simulation twice, in two processes, with one job thread and with several, and compare the final state and the plugin set : exit code 2 if a plugin (or the engine) makes the simulation depend on the thread count |
| `--state-out <file>` | write the final state hash and the plugin set hash to a file |
| `--port <n>` | with `--server` : answer the plugin handshake of clients, see [Determinism and multiplayer](#determinism-and-multiplayer) |
| `--offer-datapacks` | with `--port` : clients that lack a datapack can download it |
| `--check-server <host:port>` | ask a server whether the plugins loaded here are accepted, print its answer and exit (0 accepted, 3 refused) |

`SHARD_JOB_THREADS=<n>` pins the number of job threads of any executable.

### Safe mode

If the process crashes while plugin code runs or loads, the crash handler notes which plugin it was in `.cache/plugin_safe_mode.json` next to the project. The next run leaves that plugin out and says so in the log. The note is used once : the run after that tries the plugin again.

## The manifest

```json
{
  "schema": 1,
  "id": "com.acme.weather",
  "name": "Weather System",
  "version": "1.4.2",
  "kind": "module",
  "moduleKind": "mod",
  "linkage": "dynamic",
  "hosts": ["engine", "tool"],

  "engine": { "api": "1.0", "versions": ">=0.1.0 <0.3.0", "requires": ["largeWorld"] },

  "binaries": {
    "linux-x86_64":   "bin/linux-x86_64/libweather.so",
    "windows-x86_64": "bin/windows-x86_64/weather.dll"
  },

  "dependencies": [
    "shard.audio",
    { "id": "com.acme.clouds", "version": "^2.1", "optional": true },
    { "id": "service:weather.query" }
  ],
  "conflicts": [ { "id": "com.other.weather" } ],
  "provides": [ { "service": "weather.query", "version": "1.0" } ],
  "loadAfter": [ "com.acme.clouds" ],

  "simulation": { "affects": true, "deterministic": true },
  "content": { "mount": "content", "assetNamespace": "acme.weather", "overrides": ["materials/*"] },
  "budgets": { "frameMs": 1.5 },
  "permissions": ["render.feature"],
  "settings": { "rain_rate": 0.2 }
}
```

| Field | Meaning |
|---|---|
| `id` | lowercase letters, digits, `-` and `_`, in dot separated segments. Never changes. Prefixes the plugin's systems (`<id>/name`) and console variables (`<id>.name`) |
| `version` | semantic version `major.minor.patch` |
| `kind`, `moduleKind` | `plugin` or `module` ; for a module `engine`, `genre`, `code` (the default), `mod` or `datapack` |
| `linkage` | `static` for what is compiled into the executable, `dynamic` (default) for a library on disk |
| `tier` | `0` first-party, `1` native third-party, `3` datapack (`2`, a sandboxed plugin, is refused by this build). Defaults to 0 for code, 3 for a datapack |
| `hosts` | `engine` (= `editor`, `player` and `headless`), `editor`, `player`, `headless`, `tool` |
| `engine.api` | the plugin API it was built against. Refused if the major differs or the minor is above the engine's |
| `engine.versions` | engine versions it works with |
| `engine.requires` | engine features it needs : `largeWorld`, `determinism`, `headless`, `editor`, `physics`, `audio`, `renderer` |
| `abiFingerprint` | optional : the build it expects (see [Building](#building)) |
| `binaries`, `entry` | the library per platform (`linux-x86_64`, `windows-x86_64`, `macos-arm64`...), and its entry point (`shard_plugin_describe`) |
| `dependencies`, `conflicts` | other plugins by id and version range ; `optional` for a dependency that may be absent. `service:<name>` stands for any plugin that provides that service |
| `provides` | services it offers |
| `loadAfter`, `loadBefore` | ordering hints between plugins |
| `simulation.affects` | `true` if the plugin changes what the simulation computes. Defaults to `true` for modules |
| `content` | `mount` : the folder (inside the plugin) of its content ; `assetNamespace` : defaults to the id ; `overrides` : paths of lower layers it replaces (`*` for any, `folder/*` for a folder) |
| `budgets` | `frameMs` : milliseconds per frame it should stay under. `memoryMB` : megabytes of counted memory (`PluginContext::Memory()`, see below), and `enforce` : an allocation that would pass it fails instead of only being reported. A plugin over its budget is reported in the log and `plugin.budgets` |
| `permissions` | what it says it will do (`fs.read:content`, `render.feature`, `net`...). Read by `PluginContext::Has`, not enforced : see [Trust levels](#trust-levels) |
| `settings` | defaults for its console variables (key relative to the plugin's variables), applied below the project's values |
| `tags` | the tags it declares (`{ "Weather.Rain": { "comment": "..." } }`, or an array of names) : in the tree while the plugin is loaded, shown but not editable in Project settings > Tags. See [Tags](tags.md) |

A datapack has the same manifest with `moduleKind: "datapack"`, `tier: 3` and no `binaries`.

`ShardHeadless --validate-plugin <folder>` reports every problem of a manifest at once.

## How plugins start and stop

```
Resolved > Loaded > Initialized > Enabled <> Disabled > Unloaded
                                     \
                                    Failed
```

At startup, before the engine initializes, the manager :

1. Reads the manifests of the plugins folders, the project's `path` entries, `SHARD_PLUGIN_PATH` and `--plugin`. A broken manifest is reported and skipped.
2. Drops what does not fit : another host, engine version, plugin API, engine feature, platform, a project that turned it off. Keeps one version per id. Drops plugins whose dependencies cannot be met (and, one after the other, what depended on them), and one side of each conflict (the id that sorts last, unless the project requires it).
3. Fixes the load order : the engine's own modules first, then dependencies and `loadAfter` / `loadBefore`, ties broken by id. The order never depends on how the files were listed.
4. Loads the libraries, then calls `OnLoad` on every plugin in that order, then `OnInitialize` on every plugin.
5. Mounts the content of the modules and calls `OnContentMounted`.

Then the engine builds its systems, the plugins' systems and subscriptions join the schedule, and `OnEnable` runs on every plugin just before the first world loads. On the way out, `OnDisable` runs in reverse order, everything the plugins registered is removed, and the libraries are unloaded.

A plugin that throws, or cannot load, becomes `Failed` with the reason (see `plugin.info`), along with the plugins that depend on it. The engine keeps running, unless the plugin is `required` by the project.

**Disabling** a plugin (`plugin.disable <id>`, the Plugins panel) stops it and the plugins that depend on it, and removes everything it registered : systems, subscriptions, components, variables, commands, services, hooks, content. Its library stays loaded. **Enabling** it runs `OnLoad`, `OnInitialize` and `OnEnable` again, on the same object. A failed plugin is started over from its files. While a world is loaded, a plugin that adds components cannot be disabled (nor one that such a plugin depends on) : the world holds instances of them. Unload the worlds first, or use `plugin.reload`, which sets them aside and puts them back. The engine's own modules cannot be disabled.

A system that throws is caught ; the error is logged, and after ten of them the plugin is switched off.

## Writing a plugin

A plugin is a class deriving from the base for its kind, exported with `SHARD_PLUGIN` :

```cpp
#include "engine/plugins/context.hpp"
#include "engine/plugins/plugin.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;

class AtomShooter final : public IModule {
public:
    ModuleKind Kind() const override { return ModuleKind::Code; }

    void OnLoad(PluginContext& ctx) override {
        ctx.Components().Register<GunComponent>("game/Gun");

        Core::Ecs::SystemDesc fire;
        fire.name = "fire";
        fire.phase = Core::Ecs::Phase::Fixed;
        fire.Reads<Transform>().Writes<GunComponent>();
        fire.run = [](Core::Ecs::SystemContext& s) { /* ... */ };
        ctx.Systems().Add(std::move(fire));

        ctx.Events().Subscribe<Events::KeyPressedEvent>(
            [this](const Events::KeyPressedEvent& e) { /* ... */ });

        m_FireRate = ctx.CVars().Float("fire_rate", 8.0f, "Shots per second");
    }

private:
    Core::CVar<float>* m_FireRate = nullptr;
};

SHARD_PLUGIN(AtomShooter)
```

### Callbacks

| Callback | Called | Use it to |
|---|---|---|
| `OnLoad` | after the library is loaded | register everything : components, systems, variables, commands, services, hooks. Don't touch worlds or the GPU yet |
| `OnInitialize` | once every plugin has run `OnLoad` | ask for the services of other plugins |
| `OnContentMounted` (modules) | the content is readable | read your data tables |
| `OnEnable` / `OnDisable` | the plugin starts / stops running | start or stop work of your own |
| `OnUnload` | right before the library is unloaded | release what you own |
| `OnWorldLoaded`, `OnWorldUnloaded`, `OnPlayStart`, `OnPlayStop` (modules) | for each world, in module load order | set up per-world state |
| `OnFrameBegin`, `OnFrameEnd` (`EnginePlugin`) | around every frame | per-frame work. The time they take counts against the plugin |
| `OnEditorInit`, `OnEditorShutdown`, `OnSelectionChanged` (`EditorPlugin`) | | add windows, follow the selection |
| `OnServerStart`, `OnServerStop`, `OnServerTick` (`ServerPlugin`) | `ShardHeadless --server` calls them at the start, after every tick and at the end | |
| `OnClientConnect`, `OnClientDisconnect` (`ServerPlugin`) | called by whatever accepts the clients (`PluginManager::NotifyClientConnect`) | `OnClientConnect` can refuse a client |
| `OnToolTask` (`ToolPlugin`) | before an offline tool runs a task | refuse it with a message |
| `OnPrepareReload`, `OnRestore` | on [hot reload](#hot-reload) | save and restore what is not a reflected field |

### The plugin context

Everything a plugin registers goes through its `PluginContext`. The context tags each registration with the plugin, so when the plugin is disabled, reloaded or unloaded the engine removes all of it. Don't call the global registries directly.

| Accessor | Registers |
|---|---|
| `Components()` | component types. `Register<T>(name)` or `Register(name, factory)` ; `AdoptStaticRegistrars()` takes the types the old `REGISTER_COMPONENT` macros declared in the library |
| `Systems()` | ECS systems, with the usual phase, `Reads`, `Writes`, `After`, `Before`. Names become `<plugin id>/<name>`, and `./x` in `After`/`Before` means this plugin's `x` |
| `Events()` | event subscriptions (`Subscribe<T>`, `SubscribeToWorld<T>`) and `Emit<T>` |
| `Commands()` | console commands |
| `CVars()` | console variables, named `<plugin id>.<name>` : `Bool`, `Int`, `Float`, `String`. Values from the manifest and the project reach them even though they are set before the variable exists |
| `Worlds()` | world extensions and world settings asset references |
| `Assets()` | `Add<T>(point, name, item)` : an item for an extension point, `Uri(path)` and `GuidOf(path)` for the plugin's files |
| `Render()` | `AddFeature` and `AddPipeline` : items for the `render.feature` and `render.pipeline` points |
| `Hooks()` | interception points, see below |
| `Services()` | interfaces shared with other plugins, see below |
| `Content()` | the mounted content of all modules |
| `Memory()` | memory taken from the host's allocator and counted against `budgets.memoryMB` : `Allocate`/`Free`, `New<T>`/`Delete`, and `PluginAllocator<T>` for the standard containers. What is still allocated when the plugin unloads is reported as a leak |
| `MakeRandomStream(name)` | a seeded random stream of this plugin |
| `Log()`, `Warn()`, `Error()` | the log, tagged with the plugin id |
| `Has(permission)` | whether the manifest asked for it |

### Extension points

`Assets().Add` and `Render()` publish items under a named point in the `ExtensionRegistry` (`PluginManager::Get().Extensions()`). Whoever owns the point reads its items, and an item leaves the registry (and the engine) with its plugin. These points are read by the engine :

| Point | Item type | What the engine does with it |
|---|---|---|
| `render.feature` | `Rendering::RenderFeature` | `Render().AddFeature<Rendering::RenderFeature>(name, feature)`. Activated on the graphics thread, called with each frame's snapshot, deactivated before the plugin goes. `render.features` in the console lists them |
| `render.pipeline` | `Rendering::RenderPipelineExtension` | `Render().AddPipeline<Rendering::RenderPipelineExtension>(name, item)`. A new kind of pipeline that `.renderpipeline` assets and `render.pipeline` can name. If it is the one drawing when its plugin goes, the forward pipeline takes over |
| `asset.type` | `Core::Resources::AssetTypeExtension` | a kind of asset with its loader and file extensions. The engine gives it a kind id (`item->kind`), files with its extensions are indexed as assets of the project (`Filesystem::Type::T_CUSTOM`), and what is cached of the kind is dropped when the plugin goes |
| `asset.importer` | `Filesystem::AssetImporterExtension` | converts a source format into files the engine reads. `asset.import <file> [folder]` runs it, `Filesystem::AssetImporters::Get().Import` does from code |
| `editor.panel` | `EditorPanelItem` | `AddEditorPanel(ctx, "My window", draw)` from `OnEditorInit` adds a window to the *Window* menu |
| `tool.validator` | `ToolValidatorItem` | a check `ShardAssetValidator` runs : it gets the project file and returns the problems it found |

Pass the base type explicitly when you add something that derives from it (`Add<Rendering::RenderFeature>(name, std::make_shared<MyFeature>())`), the engine finds items by that type. Other names can be used between plugins and engine code that agree on them.

#### Drawing with ImGui

An editor plugin that draws windows needs ImGui, and a library has its own copy of ImGui's globals. Build it with `shard_add_plugin(... EDITOR_UI)` (ImGui is compiled into it), include `engine/plugins/editor_imgui.hpp` and call `BindEditorImGui(editor)` first thing in `OnEditorInit` : it gives the library the editor's ImGui context and allocators. Its panel callbacks can then call ImGui as usual.

### Hooks

Events tell you something happened. A hook lets you change the outcome or cancel it :

```cpp
ctx.Hooks().On<Hooks::PreWorldLoad>(Priority::Normal, [](Hooks::PreWorldLoad& h) {
    if (h.worldName == "forbidden.world")
        h.Cancel("blocked by acme.policy");
});
```

| Hook | Cancel means |
|---|---|
| `PreEngineInit`, `PostEngineInit` | |
| `PreWorldLoad`, `PostWorldLoad` | the start world is not loaded |
| `PrePlay`, `PostPlay` | play mode does not start |
| `PreFrame`, `PostFrame` | |
| `PreSave` | the world is not written |
| `AssetImported` | |
| `PlayerConnecting` | the client is refused (when the host calls `NotifyClientConnect`) |
| `PreShutdown` | |

Handlers run by priority (`Earliest` to `Latest`), then in plugin load order, then in the order they were added. A handler that cancels does not stop the later ones ; they can read `cancelled`. Hooks are found by name (`H::kName`), not by C++ type, so they work between the executable and a library.

### Services

Plugins talk to each other through services instead of including each other's headers :

```cpp
// provider, in OnLoad
ctx.Services().Provide<IWeatherQuery>("weather.query", 1, &m_Impl);

// consumer, in OnInitialize
m_Weather = ctx.Services().Get<IWeatherQuery>("weather.query", "^1.0");
...
if (m_Weather)
    float rain = m_Weather->RainAt(position);
```

A `ServiceRef` becomes empty when the provider is disabled or unloaded, so check it before use. Declare `provides` in the provider's manifest and `service:<name>` in the consumer's dependencies so that the load order and the resolution know about it.

### Rules to know

* **Register in `OnLoad`, use other plugins in `OnInitialize` or later.**
* **Use the engine's job system.** Don't create threads : a plugin with work in flight can't be unloaded.
* **Release what you own in `OnUnload`.** The host removes what went through the context ; anything else (GPU resources, files, your own threads) is yours.
* **Don't free what you didn't allocate.** Memory that crosses between the plugin and the engine has to come from the same allocator.
* **Exceptions stay inside the plugin.** The host catches them at every callback and system, but an exception can't cross a library boundary safely in general.
* **Don't read the world from a render feature.** It works from the frame's snapshot.
* **Don't include engine internals** from a genre or game module : the layering check (`scripts/check_deps.py`) enforces it.

## Building

`shard_add_plugin` (`cmake/ShardPlugin.cmake`) creates the library and writes its manifest :

```cmake
shard_add_plugin(
    NAME         weather
    ID           com.acme.weather
    VERSION      1.4.2
    KIND         module
    MODULE_KIND  mod
    HOSTS        engine
    DEPENDS      shard.audio "com.acme.clouds?@^2.1"
    ENGINE_REQUIRES largeWorld
    CONTENT_DIR  content
    SOURCES      weather.cpp
)
```

It lays the plugin out where the manager finds it : `<build>/plugins/<id>/shard-plugin.json`, `bin/<platform>/<library>`, and `content/` when `CONTENT_DIR` is given. `DEPENDS` takes `id`, `id@range`, or `id?` for an optional one. `AFFECTS_SIMULATION` or `NO_SIMULATION` set `simulation.affects` (modules affect it by default). `EDITOR_UI` compiles ImGui into the library, for an editor plugin that draws windows. The library links the engine and is built with `BUILD_GAME`, like the game module.

The library exports its entry point with `SHARD_PLUGIN(Class)`, or `SHARD_PLUGIN_WITH_LINK(Class, fn)` when it has to set up a copy of another library's statics (the game module does it for Jolt's allocator). A library does not share the executable's globals : the host hands it the engine, the component registry and the logger when it loads, and the context only holds pointers to the host's objects.

**Build fingerprint.** The library carries a string made of the OS, architecture, compiler, build type, and whether the physics are double precision and deterministic (`SHARD_DOUBLE_PRECISION_PHYSICS`, `SHARD_DETERMINISTIC_PHYSICS`). A library built another way than the executable is refused, with both strings in the message. Build plugins with the same CMake options as the engine they run in.

A plugin can also be compiled into the executable : declare it with `SHARD_STATIC_PLUGIN(Class, "<manifest json>")` (or `PluginManager::AddStaticPlugin` before startup) and give its manifest `"linkage": "static"`. It goes through the same resolution, lifecycle and registration as a library. A static library may drop a translation unit nothing references, so reference the plugin explicitly if you declare it that way.

Libraries that predate this (they export `InitializeSingletons` and `RegisterGameComponents`) still load, as a code module, with a warning. They get no build check and cannot be reloaded.

## Module content

A module's content folder is mounted in the virtual file system as `mod://<id>/`, and every file in it can be read from there :

```cpp
auto text = ctx.Content().ReadText("mod://com.acme.weather/tables/rain.json");
```

An address can't leave its mount : `..` and absolute paths are refused.

The same files form a second view, by *logical path* (`materials/stone.mat`), in layers : modules in load order. A later layer replaces an earlier one only for the paths it lists in `content.overrides`. Otherwise the earlier file is used and the clash is written to the log and shown in the Plugins panel (`plugin.conflicts`). Tables, translations and patches are merged on purpose and never clash.

Asset ids of module files come from `AssetRegistrar::GuidOf` : derived from `<asset namespace>/<path>` (case and separators don't matter), so they are the same on every machine and do not depend on when a file was first seen.

If a world holds a component of a type that is not registered (its plugin is not loaded), the component is kept as it was read and written back unchanged when the world is saved. Opening a world without the plugin loses nothing.

### Datapacks

A datapack has no code. It is a folder :

```
my_pack/
  shard-plugin.json            kind "module", moduleKind "datapack"
  content/
    worlds/arena.world         a whole world
    patches/sponza.patch       a patch of an existing world
    prefabs/*.json             an actor with its components and children
    tables/weapons.json        data tables
    lang/fr.json               translations
    components/health.json     a component made of fields
    materials/ textures/ models/ sounds/   assets
```

Everything is checked when the pack loads ; a pack with a file that doesn't have the right shape is `Failed` and the message names the file. `--validate-plugin` does the same without loading.

* **Tables** (`tables/<name>.json`, an object) are merged key by key across layers, the later layer winning : `ContentMounts::LoadTable("weapons")`.
* **Translations** (`lang/<code>.json`) are flat (`"ui.start": "..."`) or nested, merged the same way : `Translate(key, language, fallback)`.
* **Patches** change a world file as it is read, so the world on disk is untouched. A patch names the world by its file name (`room.world`) and lists operations. Actors are found by their names, separated by `/`. If one operation is wrong the whole patch is skipped, with the reason in the log.

```json
{ "world": "sponza.world", "ops": [
    { "op": "addActor",        "parent": "Room/Table", "actor": { "name": "Lamp", "components": [] } },
    { "op": "removeActor",     "path": "Room/Chair" },
    { "op": "setComponent",    "path": "Room/Lamp", "type": "light", "fields": { "intensity": 3.0 } },
    { "op": "addComponent",    "path": "Room/Lamp", "component": { "type": "..." } },
    { "op": "removeComponent", "path": "Room/Lamp", "type": "light" },
    { "op": "setSettings",     "settings": { "ambient_color": [0.1, 0.1, 0.1] } } ] }
```

* **Declared components** (`components/<name>.json`) describe fields (`bool`, `int`, `float`, `string`, `vec3`, `color`) with defaults. Each one becomes a real component type under its name : it can be added to an actor, is saved and loaded with the world, copied with the actor and edited in the inspector like a component written in C++, and code reads it with `DeclaredFieldsComponent::Get`/`Set`. A component has up to 64 fields and 1 KB of them, and the names `type`, `active` and `tags` are reserved. It has no behavior of its own : that comes from a code module.

`example_project/plugins/example-pack` is a small datapack.

## Trust levels

| Level | What | How it runs |
|---|---|---|
| 0 | first-party code : the engine's modules, your game | native, in the process, built with the engine |
| 1 | a third-party native plugin | native, in the process. Has to be signed by a trusted publisher, or allowed by the project |
| 2 | a sandboxed plugin | **not available** : a manifest with `tier: 2` is refused |
| 3 | a datapack | no code, validated against the formats above |

A native plugin can do anything the process can. The `permissions` list tells the user what the plugin means to do, but nothing holds it to that. Install native plugins only from publishers you trust. What Shard checks is who made the files, not what they do.

### Signatures

A plugin folder can carry a `shard-signature.json` : an Ed25519 signature, by a named publisher, over the plugin's id, its version and the digest of all its files (every file except the signature and the install marker, by relative path and SHA-256). Changing, adding or removing a file, or the id or version in the manifest, makes the signature invalid. A plugin with an invalid signature is always refused. A valid one is trusted if the publisher is in a trust file and its key was not revoked :

```json
{ "publishers": { "acme": "<64 hexadecimal digits>" }, "revoked": [ "<a key>" ] }
```

`plugin-trust.json` next to the project is read by default (`--trust-file` adds more). `plugin.trust <publisher> <key>` adds a publisher and saves it there ; `plugin.verify <id>` says who signed a plugin.

Where a signature is required :

* a plugin that was **installed from a registry** (`plugin.install` leaves `.shard-install.json` in its folder) must carry a valid signature of a trusted publisher to load ;
* a **native plugin** (`"tier": 1`) that is not signed that way is refused, unless the project's plugin entry says `"allowUnsigned": true` or the run has `--allow-unsigned-plugins` ;
* any other plugin loads unsigned : plugins you put in the folder yourself are yours.

## Packaging, signing and registries

`ShardPkg` is the publisher's tool :

```
ShardPkg keygen acme --out keys                  # keys/acme.key (keep it private) and keys/acme.pub
ShardPkg sign my_plugin --publisher acme --key keys/acme.key
ShardPkg pack my_plugin --out com.acme.weather-1.4.2.shardplugin
ShardPkg verify com.acme.weather-1.4.2.shardplugin --trust plugin-trust.json
ShardPkg index registry/index.json add registry/com.acme.weather-1.4.2.shardplugin --name "Acme"
```

A registry is a folder (or a web server's folder) with an `index.json` and the archives it lists. The index gives each archive's file name, size and SHA-256, its publisher, the engine versions it works with and its dependencies. Serve it over plain `http://` ; `https://` is not supported, and nothing depends on it : the archive is checked against the hash in the index, and its files against the signature.

In the editor or any executable with a project :

| Command | |
|---|---|
| `plugin.search [text]` | what the registries offer |
| `plugin.install <id> [range]` | downloads the plugin and the dependencies the registries can satisfy, checks size, SHA-256, the archive's paths and the signature, and puts them in the project's plugins folder. They load on the next start. An older version is replaced only once the new one is complete |
| `plugin.installed`, `plugin.updates`, `plugin.update <id>` | what came from registries, and newer versions inside the same major |
| `plugin.uninstall <id>` | removes a plugin installed from a registry (not while it is loaded, and nothing that was not installed that way) |

An archive is refused if an entry would leave the folder it is unpacked into (`..`, an absolute path, a drive, a backslash), if it expands by more than a thousand times, or past 512 MB per file or 2 GB in all.

## Determinism and multiplayer

A plugin whose manifest says `simulation.affects: true` changes what the simulation computes, so everyone who shares that simulation needs the same one :

* Each plugin has a hash made of its id, its version and the bytes of its content folder (the code is identified by the version : the same source gives other machine code on every platform). `plugin.hash` prints the set of running plugins that affect the simulation and the hash of that set.
* A replay records that set as `plugin` lines. `ShardHeadless` and the player refuse to play a replay when the plugins loaded now differ from the recorded ones, naming the difference. A replay with no `plugin` lines is not checked.
* `ShardHeadless --server --port <n>` answers a handshake over TCP : the client says which plugins (id, version, content hash) it runs, and the server welcomes it or refuses it, naming the difference. The `PlayerConnecting` hook and the `OnClientConnect` of the server plugins are asked too, and may refuse the client. With `--offer-datapacks`, a client that only lacks datapacks is offered them (id, version, size, SHA-256) and can fetch each one, check it and unpack it before restarting : code is never offered or sent. `PluginHandshakeServer`/`PluginHandshakeClient` (`engine/plugins/handshake.hpp`) are the two halves for a game that hosts its own clients ; `ShardHeadless --check-server host:port` asks a server from the command line.
* Plugins with `affects: false` (interface, audio, visuals) are left out of the set.

`ShardHeadless --check-plugins` runs the simulation in two processes, with one job thread and with several, and fails when the state differs : run it in CI for a project whose plugins affect the simulation.

A deterministic plugin follows the rules of [Headless mode and determinism](headless.md) : random numbers from `ctx.MakeRandomStream`, time from the fixed step, nothing from `rand()` or the clock. The stream is derived from the plugin id and the stream name, so adding another plugin doesn't change the numbers a plugin draws. Systems run in registration order, which is plugin load order, which is deterministic.

## Hot reload

A library built with `SHARD_PLUGIN` can be rebuilt and reloaded while the editor runs : `plugin.reload <id>` or the *Reload* button of the Plugins panel. The editor allows it ; the player with `--hot-reload-plugins` ; a deterministic run never does.

Windows keeps a loaded library locked, so with hot reload on the manager loads a copy from the temp folder and leaves the original free to be rebuilt. A reload :

1. asks the plugin to save its state (`OnPrepareReload` writes JSON, and returns `false` if it can't be reloaded) ;
2. sets the loaded worlds aside, since their components have code in the library (refused if a world has unsaved changes ; the worlds are put back afterwards) ;
3. stops the plugins that depend on it, unloads the library, loads the new file, runs `OnLoad`, `OnInitialize` and `OnEnable` ;
4. calls `OnRestore` with the saved state, and starts the dependents again.

If the new library does not load, the plugin is `Failed` with the reason.

## Console commands

| Command | |
|---|---|
| `plugin.list` | the plugins, state, and cost per frame |
| `plugin.info <id>` | everything about one plugin : what it registered, errors, warnings |
| `plugin.enable <id>`, `plugin.disable <id>`, `plugin.reload <id>` | |
| `plugin.services`, `plugin.hooks`, `plugin.extensions [point]` | what plugins offer and attached |
| `plugin.content [id]`, `plugin.conflicts` | the mounted files, and the clashes |
| `plugin.hash` | the simulation plugin set and its hash |
| `plugin.lock [verify\|write]` | check or rewrite `plugins.lock` |
| `plugin.validate <folder>` | check a plugin folder without loading it |
| `plugin.budgets` | time per frame and counted memory against each plugin's budget |
| `plugin.search`, `plugin.install`, `plugin.installed`, `plugin.updates`, `plugin.update`, `plugin.uninstall` | registries, see above |
| `plugin.trust <publisher> <key>`, `plugin.verify <id>` | trusted publishers and signatures |
| `asset.import <file> [folder]`, `render.features` | what plugins brought to the asset and render systems |

The editor's *Plugins* window (Window menu) shows the same list, with the problems found at startup and buttons for enable, disable and reload.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| *build fingerprint mismatch* | the plugin was built with another compiler, build type or physics option than the executable |
| *needs engine ...*, *built for plugin API ...* | `engine.versions` or `engine.api` doesn't fit this engine |
| *needs the engine feature ...* | the engine was built without it (`largeWorld` needs `SHARD_DOUBLE_PRECISION_PHYSICS`) |
| *no binary for linux-x86_64* | the manifest has no library for this platform |
| a plugin and its dependents are `Failed` | a callback threw or a dependency failed : `plugin.info <id>`, and the log lines tagged with the plugin id |
| two plugins conflict | `conflicts` in a manifest : the one that sorts last by id is left out, unless the project requires it |
| a file from a module is not used | another layer provides it and this one does not list it in `content.overrides` |
| a datapack is `Failed` | a file doesn't have the right shape : the message names it ; `--validate-plugin` shows all of them |
| the replay is rejected at start | the plugins loaded now differ from the recorded ones ; the message lists the difference |
| `plugins.lock` warnings | a plugin changed since the lock was written : `plugin.lock write` after checking why |
| a plugin is missing after a crash | safe mode left it out for one run ; `--ignore-safe-mode` loads it |
| a plugin shows in `Rejections` as untrusted | its signature is invalid, or it is native / installed and not signed by a trusted publisher : the message says which, and what to add |
| `plugin.install` says the SHA-256 differs | the archive on the server is not the one the index describes : nothing was installed |
| `plugin.install` says the plugin is unsigned or untrusted | add the publisher's key to `plugin-trust.json` (`plugin.trust`), or install with `--allow-unsigned-plugins` if you accept the risk |
| `--check-plugins` fails | a plugin that affects the simulation depends on the number of job threads : its systems write what others read without declaring it, or it uses a thread of its own |
| `plugin.reload` refuses | hot reload is off in this run, the plugin was not built with `SHARD_PLUGIN`, or a loaded world has unsaved changes |

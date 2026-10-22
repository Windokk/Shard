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

`--game <library>` still works : the library is loaded as the plugin `game`, a code module. It does not need a manifest, and it may be a C++ plugin or one built with the SDK (the engine tells them apart by the entry point they export). The example game is built with `shard_add_plugin` (id `game`, `ABI c1`, so against the SDK alone), and sits at `<build>/plugins/game/bin/<platform>/` with its `shard-plugin.json` : pass the library to `--game`, or the folder to `--plugin` to load it through its manifest. Its scripts are the components `game.Character`, `game.Gun` and `game.RandomKicker`, as every script is named after its plugin.

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
| `abi` | how the library talks to the engine : `cpp` (default) for a plugin that links the engine, `c1` for a plugin built with the [SDK](#the-c-abi-and-the-sdk). `shard_sdk_add_plugin` and `shard_add_plugin(... ABI c1)` write it for you |
| `abiFingerprint` | optional, `cpp` plugins only : the build it expects (see [Building](#building)). A `c1` plugin is not checked against the build fingerprint |
| `binaries`, `entry` | the library per platform (`linux-x86_64`, `windows-x86_64`, `macos-arm64`...), and its entry point (`shard_plugin_describe` for `cpp`, `shard_plugin_entry` for `c1`) |
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

It lays the plugin out where the manager finds it : `<build>/plugins/<id>/shard-plugin.json`, `bin/<platform>/<library>`, and `content/` when `CONTENT_DIR` is given. `DEPENDS` takes `id`, `id@range`, or `id?` for an optional one. `AFFECTS_SIMULATION` or `NO_SIMULATION` set `simulation.affects` (modules affect it by default). `EDITOR_UI` compiles ImGui into the library, for an editor plugin that draws windows. The library links the engine and is built with `BUILD_GAME`. With `ABI c1` it links nothing of the engine and sees only `sdk/include`, which is how the example game is built.

`ABI c1` builds a plugin of the [C ABI](#the-c-abi-and-the-sdk) instead : the library links nothing of the engine, only sees the headers of `sdk/include`, and its manifest says `"abi": "c1"`. Outside the engine's repository, use `shard_sdk_add_plugin` from the SDK, which lays the plugin out the same way (see [Write a game with the SDK](../tutorials/write-a-game-with-the-sdk.md)).

The library exports its entry point with `SHARD_PLUGIN(Class)`, or `SHARD_PLUGIN_WITH_LINK(Class, fn)` when it has to set up a copy of another library's statics (the game module does it for Jolt's allocator). A library does not share the executable's globals : the host hands it the engine, the component registry and the logger when it loads, and the context only holds pointers to the host's objects.

**Build fingerprint.** The library carries a string made of the OS, architecture, compiler, build type, and whether the physics are double precision and deterministic (`SHARD_DOUBLE_PRECISION_PHYSICS`, `SHARD_DETERMINISTIC_PHYSICS`). A library built another way than the executable is refused, with both strings in the message. Build plugins with the same CMake options as the engine they run in.

A plugin can also be compiled into the executable : declare it with `SHARD_STATIC_PLUGIN(Class, "<manifest json>")` (or `PluginManager::AddStaticPlugin` before startup) and give its manifest `"linkage": "static"`. It goes through the same resolution, lifecycle and registration as a library. A static library may drop a translation unit nothing references, so reference the plugin explicitly if you declare it that way.

Libraries that predate this (they export `InitializeSingletons` and `RegisterGameComponents`) still load, as a code module, with a warning. They get no build check and cannot be reloaded.

## The C ABI and the SDK

A plugin can be written against the Shard SDK (`sdk/include/shard`) instead of the engine's C++ headers. It links nothing of the engine and includes none of its headers. It loads in every engine with the same ABI major revision and a minor revision at least as recent as its own, whichever compiler built either side. A plugin built with MSVC loads in an engine built with MinGW GCC, and one built with Clang loads in an engine built with GCC. The CI checks both.

There are two layers :

* The C ABI (`shard/shard.h` and the headers it includes) is plain C11. It is the contract between the plugin and the engine.
* The C++ SDK (`shard/game.hpp` and the `shard/cpp/` headers) is header-only C++17 on top of the C ABI. It is compiled into the plugin, so it can use the standard library freely without affecting the ABI.

The C++ SDK itself has two levels. The gameplay level is how a game is written : classes that live on actors, like the engine's own scripts, in as many files as you want.

```cpp
// health.hpp
#include <shard/game.hpp>

class Health : public shard::Script {
public:
    SHARD_FIELD(float, maxHealth, Range(1.f, 1000.f)) = 100.f;      // a field : shown in the inspector, saved with the world
    SHARD_FIELD(float, current) = 100.f;

    void OnPlay() override { current = maxHealth; }
    void OnTick(float deltaTime) override { if (current <= 0.f) Owner().Destroy(); }
};

// health.cpp
#include "health.hpp"
SHARD_REGISTER_SCRIPT(Health)                           // registers the class

// module.cpp, once
SHARD_GAME_MODULE()
```

The registration level underneath is for a plugin that is not a game, or that fits in one file. It registers components, systems, variables and commands by hand in `OnLoad` :

```cpp
#include <shard/cpp/plugin.hpp>

struct Spin { float speed = 1.f; float angle = 0.f; };
SHARD_COMPONENT(Spin, (speed)(angle))

class SpinPlugin : public shard::Plugin {
public:
    void OnLoad(shard::Context& ctx) override
    {
        auto spin = ctx.Components().Register<Spin>("Spin");
        ctx.Systems().Add("rotate", shard::Phase::Update)
            .Writes(spin)
            .Run([spin](shard::SystemCtx& s) {
                s.Each(spin, [&](Spin& c) { c.angle += c.speed * s.DeltaTime(); });
            });
        ctx.CVars().Float("scale", 1.f, "multiplies every speed");
    }
};
SHARD_C_PLUGIN(SpinPlugin)
```

You can mix them. A game can have scripts in some files, systems in others (`SHARD_ON_LOAD`, `SHARD_DEFINE_COMPONENT`), and a class derived from `shard::Plugin` for variables, commands and hot reload. The [tutorial](../tutorials/write-a-game-with-the-sdk.md) builds one.

The manifest of such a plugin says `"abi": "c1"`, and its entry point is `shard_plugin_entry` instead of `shard_plugin_describe`. `shard_sdk_add_plugin` (outside the repository) and `shard_add_plugin(... ABI c1)` (inside it) write both. The [C ABI reference](../api_reference.md) lists every function.

### Getting the SDK

The SDK is a folder : `include/` (the C ABI and the C++ SDK), `cmake/` (`find_package(ShardSDK)` and `shard_sdk_add_plugin`), `bin/ShardPkg`, `templates/plugin/` and `VERSION`. Its version is the C ABI revision. To get it, run `cmake --install build --component sdk --prefix <dir>` on an engine build (it goes to `<dir>/sdk`), or build the `ShardSDKArchive` target, which writes `build/shard-sdk-<version>.zip`. The CI publishes that archive for each OS.

```cmake
find_package(ShardSDK 1.0 REQUIRED)
shard_sdk_add_plugin(weather
    MANIFEST    shard-plugin.json      # the SDK adds "abi", "linkage" and "binaries"
    SOURCE_DIRS src                    # every .cpp under src/
    CONTENT_DIR content
    PACKAGE)                           # adds the target weather_package
```

`SOURCES` lists files one by one, `SOURCE_DIRS` takes every `.cpp`, `.cc`, `.cxx` and `.c` under a folder (CMake looks again when a file is added). `PACKAGE` adds a `<target>_package` target that packs the plugin with the SDK's `ShardPkg` into `<build>/plugins/<id>.shardplugin`. Without a `MANIFEST`, give `ID` and `VERSION`, and optionally `KIND`, `MODULE_KIND`, `HOSTS`, `DEPENDS` and `AFFECTS_SIMULATION`. The manifest is then written for you.

### What the C ABI offers

A plugin asks for interfaces by name and version : `host->query_interface("shard.systems", 1, 0, &table)`. The C++ SDK does this for you.

| Interface | What it does |
|---|---|
| `shard.plugin` 1.2 | the plugin's id, version and folder. The host it runs in and its permissions (1.1). Random streams (1.2) |
| `shard.cvars` 1.0 | console and config variables |
| `shard.commands` 1.0 | console commands |
| `shard.components` 1.3 | component types described field by field, each one an ECS component and an actor component. Blobs (1.1), text and asset fields (1.2), actor references and plugin-owned strings in scripts (1.3) |
| `shard.ecs` 1.1 | queries by chunk, component access and structural changes, from inside a system. Moving the actor behind an entity (1.1) |
| `shard.systems` 1.0 | systems in the scheduler's phases, with their reads, writes and order |
| `shard.events` 1.1 | events by name, carrying bytes. C++ code reaches the same channel. Describing a payload for the tools (1.1) |
| `shard.hooks` 1.0 | the engine's hooks, by name, cancellable |
| `shard.services` 1.0 | tables of functions that plugins offer each other, by name and version |
| `shard.extensions` 1.0 | asset validators, asset types and asset importers |
| `shard.content` 1.0 | module content : `mod://` addresses, logical paths, reading files |
| `shard.state` 1.0 | what a plugin keeps across a hot reload, as JSON text |
| `shard.scripts` 1.1 | script types : classes of the plugin that live on actors. Physics contacts (1.1) |
| `shard.actors` 1.0 | the actors of a world : pose, hierarchy, tags, creating and finding them, the engine's components as JSON |
| `shard.input` 1.0 | keyboard, mouse and gamepads |
| `shard.physics` 1.0 | velocities, forces, impulses, gravity and ray casts |
| `shard.audio` 1.0 | the sound of an actor, one shot sounds |
| `shard.worlds` 1.0 | loading a world, spawning a prefab by name |
| `shard.editor` 1.0 | panels in the editor. Only the editor offers it |
| `shard.experimental.world_ecs` 0.1 | the engine's entities from the main thread, outside systems (experimental, see below) |

The lifecycle callbacks are those of `IPlugin`, plus the frame, content and world callbacks of `EnginePlugin` and `IModule`, and `on_tool_task` of `ToolPlugin`.

The C ABI has no render features or pipelines, no ImGui itself (a plugin draws with the editor's widgets, see below), no physics shapes or joints beyond what the body component's fields describe, no audio mixing or effects, and no network. The engine components that have no interface of their own can still be read and changed as JSON through `shard.actors`. A plugin that needs more is written in C++ against the engine.

### Scripts, actors and input

A script type (`shard.scripts`) is a class of the plugin that lives on an actor. The plugin describes its size, its fields and its callbacks, and the engine registers it as an actor component named `<plugin id>.<name>`. It is built on the engine's own `Script` component, so :

* the world ticks it in the same order as the engine's scripts (the order the components were added),
* play mode starts and stops it,
* the world file saves its fields and the inspector shows them,
* a copy of the actor made for play mode builds a new object and copies the fields into it.

The object lives in storage held by the component, up to 16 KB. The plugin's own functions construct and destroy it in place and the engine never moves it, so it can have any member, including a `std::vector`. Fields are described like a component's, with `ShardFieldDesc` : numbers, booleans, vectors, quaternions, colours, text, assets and blobs, at their offsets in the object.

The callbacks are `OnCreate`, `OnPlay`, `OnTick(dt)`, `OnFixedTick(dt)`, `OnLateTick(dt)`, `OnStop`, `OnDestroyed`, `OnActivated`, `OnDeactivated`, `OnWorldLoaded` and `OnWorldUnloaded`. They mean what they mean for the engine's scripts.

The scripts of a world run one after the other on the thread that ticks them, never two at once. This is why they can use `shard.actors` and `shard.input`, which a system cannot (systems run side by side). A script can destroy its own actor or add components in a callback : the world ticks the scripts it had when the tick began and skips the ones removed in the meantime.

`shard.actors` works with `ShardActor` handles, made of a generation and a slot. A destroyed actor fails with `SHARD_E_STALE_HANDLE`, and its handle never refers to another actor. It can :

* read and set the pose, the name, the active state, the parent, the children and the tags,
* create an actor, spawn one from the JSON a world file holds, find one by name or by tag, and destroy one,
* add, test, remove, read and set the engine's components by the name their serialized form uses (`"light"`, `"model"`, `"physics_body"`), with their fields as JSON. Anything the editor shows can be reached this way.
* read, set and erase one field of a component (`shard.actors` 1.1), by its name or a path into it (`"materials/0"`, `"shapes/0/params/radius"`, `"shapes/-"` to append). A field of the engine is set the way an edit in the inspector sets it, so a physics body keeps its velocity when its friction changes. The C++ SDK wraps these in typed handles (`actor.Add<shard::Model>().SetMesh(mesh)`, `shard::PhysicsBody`, `shard::Light`...).

Creating and searching work in the world of an actor that you pass in. A script passes its owner.

`shard.input` reads the engine's input manager, which is what replays record, so a script that reads its input here behaves the same in a replay. Key and button numbers are part of the ABI.

**Random numbers** (`shard.plugin` 1.2). `rand()`, `std::random_device` and `std::uniform_*_distribution` give other numbers from one run, platform or standard library to the next, so a replay that used them would drift. `random_stream` returns the state of a generator for a named stream, derived from the run's seed, the plugin's id and the name : the same name gives the same sequence when the run is replayed. The generator is PCG32, the engine's own `Core::Random`, and the SDK's `shard::Random` runs it inside the plugin, so drawing a number is not a call into the engine and gives the same bits. Name a stream after what draws from it (`"Spawner/" + Owner().Name()`), not after the order scripts started in.

```cpp
void RandomKicker::OnPlay() { m_Random = shard::Random::Stream("RandomKicker/" + Owner().Name()); }
void RandomKicker::OnTick(float) { Owner().AddImpulse(m_Random.OnUnitSphere() * strength); }
```

**Contacts** (`shard.scripts` 1.1). A script with a physics body is told when it touches another body : `on_contact_added`, `on_contact_persisted` and `on_contact_ended` (`OnContactAdded`, `OnContactPersisted`, `OnContactEnded` in C++). Each gets a `ShardContact` : the other actor, a point of the contact, the normal and the penetration depth. The normal points from the script's actor toward the other one. The end of a contact has the other actor only. Both actors of a pair get the call, each with its own point of view.

**Actor references and strings in scripts** (`shard.components` 1.3). A script field of type `SHARD_FIELD_ACTOR` holds a `ShardActor`. The world file saves it as the path of the actor, `"Level/Enemy"` (root first, then the children's names). When the file is loaded, or a copy is made for play mode, the actor named by the path may not exist yet : the engine keeps the path, looks again before each callback, and fills the handle on the first call where the actor is there. An actor that is destroyed leaves an empty reference, never a dangling one. In C++ the field is a `shard::Actor` (check it with `if (target)`). References are for scripts only, because a plain component block has no way to hold a path. They count against the same 8 text fields as strings.

A script can also list a string that the plugin owns, a `std::string` in C++. Such a field is flagged `SHARD_FIELD_ACCESSOR`, and gives `text_get` and `text_set` in the script's callbacks so that the engine reads and writes it through the plugin. The `std::string` itself never crosses the ABI, which is what keeps it safe between compilers : only the bytes do. Components still use `shard::Text`.

**Physics** (`shard.physics`). Takes the handle of an actor with a `physics_body` component. It reads and sets the velocity and the angular velocity, adds forces, torques and impulses (at the centre or at a point of the world), reads the gravity that acts on the body and gives it a gravity of its own. A force lasts for the step it is added in, so a script that wants to push for longer adds it every fixed tick. Units are metres, kilograms, seconds and newtons.

`raycast` returns the first body on a ray : its actor, the point, the surface normal and the distance. `layers` limits it to static bodies, moving bodies or both, and `ignore` is an actor the ray goes through, which is usually the one firing it.

```cpp
void Turret::OnFixedTick(float)
{
    auto hit = shard::Physics::Raycast(Owner().WorldPosition(), Owner().Forward(), 50.f, shard::Layers::All, Owner());
    if (hit) hit->actor.AddImpulse(Owner().Forward() * 5.f);
}
```

**Audio** (`shard.audio`). `play`, `pause` and `set_volume` act on the `audio` component of an actor. `play_one_shot` plays a clip of the project once, from a place in the world or flat, with no actor, and frees itself when it ends. A host with no sound device accepts every call and plays nothing.

**Worlds and prefabs** (`shard.worlds`). `load_world` asks for a world by its path in the project. The load is a request : it runs in the background and the world is swapped in at the start of a later frame, so the script that asked keeps running with everything it holds until then. `is_loading` and `loaded_count` tell where it is. `spawn_prefab` builds an actor from a JSON file of the plugin's content, by `mod://` address, logical path (`prefabs/crate.json`) or bare name (`crate`), under a parent or at the root of the world. Before this, a plugin had to pass the actor's JSON itself.

**Panels in the editor** (`shard.editor`). ImGui has no stable ABI, so a plugin never calls it. It adds a panel, a window of the editor's Window menu, and draws its contents with the widgets `ShardUiApi` offers : text, buttons, checkboxes, sliders, drags, text boxes, colour pickers, combos, trees, progress bars and plots. The draw function runs every frame the window is open, on the editor's main thread, and only then may it use the widgets. Only the editor offers `shard.editor`. In the player, in a headless run and in the offline tools the interface is not found, the panel is not added, and the same plugin loads everywhere. A plugin whose `hosts` include `engine` is loaded by the editor like by the player, and `editor` alone limits it to the editor.

```cpp
SHARD_EDITOR_PANEL(Spawner, "Enemy spawner", ui)
{
    static int count = 5;
    ui.SliderInt("Count", count, 1, 50);
    if (ui.Button("Spawn")) SpawnEnemies(count);
}
```

A call into a script goes through the plugin like any other. The plugin is named as the one running, so a crash is blamed on it. An exception is caught, and an error returned counts as an error of the plugin : at the tenth, the engine fails the plugin, as it does for a system. When the plugin is disabled or unloaded, its scripts stop being called, so a world that still holds them never calls into a library that has left.

### Components across the ABI

A type that a plugin registers has two parts under one name (`<plugin id>.<name>`) :

* an ECS component : a block of plain data on an entity, copied with `memcpy` and never constructed. Systems query it and write it.
* an actor component : what a world file saves, the inspector shows and an actor carries. It is made of the fields the plugin described (floats, integers, booleans, vectors, quaternions, colours, and since ABI 1.3 text and assets).

When an actor has the component, its entity has the block with the same values. The actor component writes its values to the entity when it is loaded, edited, attached or activated, and reads them back when the world is saved or copied for play mode. What a system changed is therefore what gets saved. A deactivated component leaves the entity, so systems skip it. An entity that a plugin spawned itself has no actor and is not saved.

Field flags : `SHARD_FIELD_RUNTIME` shows the field read-only and does not save it, `SHARD_FIELD_PRIVATE` neither shows nor saves it. A component is at most 1024 bytes, aligned on at most 16, with at most 64 fields. Its ECS id is kept for the whole run : a plugin that is enabled again gets the same id back, as long as the size did not change (a new layout needs a restart).

**Blobs** (ABI 1.2) hold what does not fit in the block. A blob is bytes that the engine keeps outside the block, counted against the plugin's memory budget and released with the plugin. A field of type `SHARD_FIELD_BLOB` holds its handle. The block is still plain data, so copying a block shares the blob. The actor component is the exception : it owns the blobs of its blob fields, saves their bytes with the world (base64), gives a copy of the actor copies of them, and frees them when the component is removed.

**Text and assets** (ABI 1.3). A field of type `SHARD_FIELD_STRING` holds the handle of a blob whose bytes are UTF-8 text. The block stays plain data, the inspector shows a text box, and the world file saves a string. Ownership works as for blobs. `string_set` and `string_get` of `shard.components` 1.2 read and write it without touching the blob (`shard::Text` in C++, with `Get()` and `Set()`). A component has at most 8 string fields, and a string field cannot be `SHARD_FIELD_PRIVATE`.

A field of type `SHARD_FIELD_ASSET` is an `int32_t` : the id of an asset of the project, 0 for none (`shard::AssetId` in C++). The inspector shows an asset picker, and the world file saves the asset by its name in the project, like the engine's own components. Neither type can be used in a described event, because an event payload is copied as bytes.

### Moving actors

A system cannot write the engine's own pose component through the pointers a query returns. The actor's `Transform` drives `shard.LocalTransform` and would overwrite the change. Instead, a system calls `set_local_transform` of `shard.ecs` 1.1 (`SystemCtx::SetLocalTransform(entity, shard::Pose)` in C++), after declaring `shard.LocalTransform` in its `writes` :

```cpp
auto local = ctx.Components().Find<shard::Pose>("shard.LocalTransform");
ctx.Systems().Add("drift", shard::Phase::Update)
    .Writes(sign, local)
    .Run([sign](shard::SystemCtx& s) {
        s.Each(sign, [&](shard::Entity e, Sign& c) {
            shard::Pose p;
            p.position.x = float(++c.uses);
            s.SetLocalTransform(e, p);
        });
    });
```

The pose is relative to the parent. It is recorded and applied when the phase ends, on the main thread, after the structural changes. The actor's `Transform` takes it, so children, physics and the renderer follow, and the entity's `LocalTransform` then holds it. Until the phase ends, systems that read `shard.LocalTransform` still see the old pose, whether the phase runs on one thread or many.

If several systems set the same entity in one phase, the one whose name sorts last wins. Only what changed is applied, so setting the same pose every frame costs nothing. A deactivated actor ignores the call, and an entity with no actor (one the plugin spawned itself) gets `SHARD_E_NOT_FOUND`.

### Systems and threads

A system declares what it reads and writes, and can reach only those types through `shard.ecs` (an exclusive system can reach any). The scheduler runs the systems of a phase in parallel when they do not conflict, and the result is the same with one thread or many. `ShardHeadless --check-plugins` checks this.

Every function of the ABI says which thread may call it : `any`, `system` (from a system, so possibly in parallel) or `main` (the main thread, outside systems). A `main` function called from a system returns `SHARD_E_WRONG_THREAD`. An event emitted from a system is queued and delivered on the main thread at the end of the frame, with the queues taken in the order of the systems' names, so the order does not depend on which system finished first.

A system that returns an error, or throws in C++, counts as an error of the plugin. At the tenth, the engine fails the plugin, as for a C++ system. Every call from the engine into a C plugin (lifecycle, systems, events, hooks, commands, validators, importers, asset loaders) names the plugin as the one running, so a crash is blamed on it and the next start offers safe mode without it.

### Described events

The payload of a named event is bytes, and the sender documents their layout. Since ABI 1.2 the sender can also describe it to the engine, with the same fields as a component (`describe` in `shard.events` 1.1, `ctx.Events().Describe<T>()` in C++, `EventRegistrar::DescribeNamed` for a C++ plugin). The console then uses the description :

* `event.list` lists the described events and their fields,
* `event.watch <name>` logs every event sent under that name with its values (`{ ticks: 3, axis: (0, 1, 0) }`),
* `event.unwatch <name>` stops.

### Experimental interfaces

Things that may still change are offered as `shard.experimental.*` before they are frozen into the ABI. These interfaces come with no compatibility promise : the major is 0, `query_interface` returns one only for its exact minor, and it can change or disappear in any engine revision. A native plugin carrying a valid signature from a trusted publisher (a distributed plugin) is refused them. They are for development, and for trying what the next ABI revision should contain.

There is one today, `shard.experimental.world_ecs`. It gives the engine's registry on the main thread (`shard::experimental::World` in C++). It is opened and closed within one callback, and its structural changes are applied when it closes.

### Versions and the rules of the headers

The ABI has a major and a minor revision (`SHARD_ABI_MAJOR`, `SHARD_ABI_MINOR`, also the SDK's version). A minor revision only adds : functions at the end of a table, fields at the end of a struct, new interfaces. A plugin built for 1.0 therefore loads in an engine at 1.1, while a plugin built for 1.1 is refused by an engine at 1.0 (`built for the C ABI 1.1, this engine has 1.0`). Each interface has its own version as well : a plugin that needs `has_permission` asks for `shard.plugin` 1.1.

The headers follow these rules :

* plain C, with fixed-size types in structs,
* every struct starts with `uint32_t structSize` and only grows at its end,
* handles (index and generation) stand for host objects, never pointers,
* no exception crosses the boundary.

A handle that outlived its object (a cvar of a disabled plugin, a `ShardEcs` kept after the system's run) fails with `SHARD_E_STALE_HANDLE` instead of reading freed memory.

The build fingerprint does not apply to a `c1` plugin, since the ABI revision is the contract. The tier does not change either : a C plugin is a native tier 1 plugin, signed or allowed like the others, and nothing isolates it.

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
ShardPkg fingerprint                             # the build fingerprint of this engine, what `cpp` plugins are checked against
```

Sign first, then pack, then index : signing writes `shard-signature.json` into the folder, and the archive is made of the folder as it is. Without `--out`, `pack` names the archive `<id>-<version>.shardplugin`. The archive holds the manifest, the libraries and the content, and nothing else : the import library a Windows build produces stays in the build folder.

`ShardPkg` is installed in the SDK (`sdk/bin`) (on Windows with MinGW it is linked statically, so it runs without the engine or a compiler runtime next to it), so a plugin built outside the engine's repository can pack and sign itself : `shard_sdk_add_plugin(... PACKAGE)` finds it there (`SHARD_PKG_EXECUTABLE` points CMake at another one). It works the same way for `cpp` and `c1` plugins.

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
| `event.list` | the named events that plugins described (ABI 1.2) and their fields |
| `event.watch <name>`, `event.unwatch <name>` | log every event sent under that name with its values, or stop |
| `asset.import <file> [folder]`, `render.features` | what plugins brought to the asset and render systems |

The editor's *Plugins* window (Window menu) shows the same list, with the problems found at startup and buttons for enable, disable and reload.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| *build fingerprint mismatch* | a `cpp` plugin was built with another compiler, build type or physics option than the executable. A plugin built with the SDK (`"abi": "c1"`) is not checked this way |
| *built for the C ABI 1.x, this engine has 1.y* | the plugin was built with an SDK more recent than the engine : update the engine, or build against an older SDK |
| *does not export the C ABI entry point* | the manifest says `"abi": "c1"` but the library has no `shard_plugin_entry` : build it with `shard_sdk_add_plugin` or end it with `SHARD_C_PLUGIN` |
| *'abi' must be "cpp" or "c1"* | a typo in the manifest's `abi` field |
| an interface call returns `SHARD_E_UNSUPPORTED` | an experimental interface (`shard.experimental.*`) asked by a plugin that is signed by a trusted publisher, or an engine that does not have it |
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

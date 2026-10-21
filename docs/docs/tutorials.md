# Tutorials

## Write a plugin outside the engine repository

This builds a plugin with the Shard SDK alone : a folder of headers and a CMake package. No engine source, no engine library, and any C++17 compiler (GCC, Clang, MSVC), whatever compiler built the engine. The result is a component, a system that updates it every frame and a console variable, loaded by the engine.

### 1. Get the SDK

From an engine build :

```sh
cmake --install build --component sdk --prefix <dir>
```

The SDK is then in `<dir>/sdk` : `include/` (the headers), `cmake/` (the package), `bin/ShardPkg` (packs and signs plugins), `templates/plugin/` and `VERSION`. The `ShardSDKArchive` target makes the same folder as `build/shard-sdk-<version>.zip`, and every CI run publishes one per OS (artifacts `shard-sdk-Windows`, `shard-sdk-Linux`). Inside the engine's repository, the `sdk/` folder is an SDK too, without `bin/`.

### 2. Start from the template

Copy `sdk/templates/plugin` somewhere of your own :

```
my-plugin/
  CMakeLists.txt        find_package(ShardSDK 1.0) and shard_sdk_add_plugin : rename the target (template_plugin) and the project as you like
  shard-plugin.json     the manifest : change its "id" (reverse DNS, lowercase : com.yourname.myplugin)
  src/plugin.cpp        the code
  content/              files the plugin reads through mod://<id>/
```

### 3. Build it

```sh
cmake -S my-plugin -B my-plugin/build -DCMAKE_PREFIX_PATH=<dir>/sdk
cmake --build my-plugin/build
```

The plugin lands in `my-plugin/build/plugins/<id>/` : its manifest (the SDK added `"abi": "c1"` and the path of the library), `bin/<platform>/` and `content/`. The library exports one symbol, `shard_plugin_entry`. Built by MinGW it carries its own C++ runtime ; built by MSVC it needs the Visual C++ runtime, like any program MSVC builds.

### 4. Load it

Give the folder to the engine :

```sh
ShardHeadless --game <game library> --project <project.json> --plugin my-plugin/build/plugins/<id> --list-plugins
```

or copy it into the project's plugins folder, where the editor and the player find it. `--check-plugins` runs the simulation twice, with one job thread and with several, and fails if the state differs : what a plugin that affects the simulation must survive.

If the engine answers `built for the C ABI 1.2, this engine has 1.1`, it is older than the SDK : build against an SDK at most as recent as the engines you target (step 6).

In the editor, the component appears in the inspector's *Add component* menu as `<id>.Spin`. Put it on an actor : whenever the simulation runs, the system turns its `angle`, and saving the world saves the values the system wrote.

### 5. Make it yours

What the template uses, and where to read more :

* `ctx.Components().Register<T>("Name")` : `SHARD_COMPONENT(T, (field)(field))` lists the saved fields ; the struct's default member values are the defaults. Options per field (bounds, tooltip, `SHARD_FIELD_RUNTIME`) go in the second argument.
* `ctx.Systems().Add("name", shard::Phase::Update).Reads(a).Writes(b).Run(fn)` : declare every type the system touches ; `s.Each(type, fn)` or `s.Query(a, b).Without(c).Each(fn)` visit the entities. Structural changes (`s.Spawn`, `s.Add`, `s.Remove`, `s.Destroy`) are applied at the end of the phase.
* `ctx.CVars()`, `ctx.Commands()`, `ctx.Events()`, `ctx.Hooks()`, `ctx.Services()`, `ctx.Content()`, `ctx.Extensions()` : see [Plugins](concepts/plugins.md#the-c-abi-and-the-sdk) and the [C ABI reference](api_reference.md).
* `OnPrepareReload` / `OnRestore` keep values across a hot reload in the editor.
* What does not fit in a component's 1024 bytes goes in a `shard::Blob` member : `ctx.Components().CreateBlob(size)`, `BlobData(blob)`. The actor component saves its bytes with the world.
* `ctx.Events().Describe<Payload>("name")` tells the tools what an event carries : the console's `event.watch <name>` then prints each one with its fields.
* `shard::experimental::World` reaches the engine's entities from the main thread, outside the systems. Experimental : it may change in any engine revision, and a signed plugin is refused it.

A registration the host refuses throws `shard::Error` : uncaught, it fails the plugin with the message in the log. Nothing a plugin throws crosses into the engine.

To write it in C instead, include `<shard/shard.h>` and fill the tables yourself : `tests/plugin_fixtures/c_plugin/c_plugin.c` in the engine's repository uses every interface.

### 6. Ship it

The template asks for `PACKAGE` : `cmake --build my-plugin/build --target template_plugin_package` makes `my-plugin/build/plugins/<id>.shardplugin` with the SDK's own `bin/ShardPkg` (`SHARD_PKG_EXECUTABLE` points at another one). The archive holds the manifest, the library and the content, nothing else. See [Packaging, signing and registries](concepts/plugins.md#packaging-signing-and-registries) to sign it : a signed plugin is refused the experimental interfaces.

A plugin built for SDK 1.x loads in every engine of ABI 1.x whose minor revision is at least the SDK's. To reach as many engines as possible, build against the oldest SDK that has what you use.

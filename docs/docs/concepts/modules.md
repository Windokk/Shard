# Modules

A module is a part of a Shard project that brings components, systems and content, and follows the lifecycle of the worlds. The engine itself is made of modules, the genre toolkits (voxel, planetary, racing...) are modules, and so is your game. They all register what they add through the same API, so a feature written inside the engine and one written in a game look the same to the rest of the code.

Modules are [plugins](plugins.md) : they have a manifest, dependencies, a load order, and can be enabled, disabled and reloaded. This page is about what is specific to modules.

## What the engine is made of

Run `plugin.list` in the console (or open the Plugins window in the editor) :

```
#   id                                version     kind      state       tier ms/frame  notes
1   shard.assets                      0.1.0       engine    Enabled     0    0.00      static
2   shard.audio                       0.1.0       engine    Enabled     0    0.00      static
3   shard.core                        0.1.0       engine    Enabled     0    0.00      static
4   shard.physics                     0.1.0       engine    Enabled     0    0.00      static
5   shard.renderer                    0.1.0       engine    Enabled     0    0.00      static
6   shard.world                       0.1.0       engine    Enabled     0    0.00      static
7   game                              0.0.0       code      Enabled     0    0.00      sim
```

The `shard.*` entries are the engine's own modules. They are compiled into the executable and always load first, so a plugin can depend on them (`"dependencies": ["shard.physics"]`) to say what it builds on. They cannot be disabled.

The game library you pass with `--game` is the module `game`.

## What a module can do

| | |
|---|---|
| Add components and systems | `ctx.Components()`, `ctx.Systems()`. Systems join the schedule with the same `reads` / `writes` as the engine's, and run in plugin load order |
| Follow the worlds | `OnWorldLoaded`, `OnWorldUnloaded`, `OnPlayStart`, `OnPlayStop` are called for every world |
| Carry content | files mounted as `mod://<id>/`, layered over each other, with tables and translations merged |
| Patch worlds | datapacks change a world file as it is read |
| Provide services | an interface other modules ask for by name and version |
| Declare components | a datapack can describe a component made of fields, which then behaves like any other |

## Rules between modules

* A module reaches another one through components, events or services, not through its headers.
* A genre module or a game module includes the public headers only. `python scripts/check_deps.py` checks the layering in CI.
* A module that draws gets the frame's snapshot, never the world.

See [Plugins and modules](plugins.md) for the manifest, the API, the build and the datapack formats.

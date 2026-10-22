# A Shard game, as a plugin

Built against the Shard SDK alone : no engine source, no engine library, any C++17 compiler.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=<the SDK folder>
cmake --build build
ShardHeadless --game <game library> --project <project.json> --plugin build/plugins/com.example.template --list-plugins
```

## The layout

```
shard-plugin.json        the manifest : change `id` first (reverse DNS, lowercase), then `name`, `description`, `hosts`
src/
  module.cpp             the plugin's entry point : console variables, content, hot reload (the part that is not gameplay)
  module.hpp             what the files share with it
  gameplay/spinner.*     a script : a class on an actor with OnPlay / OnTick..., its fields saved with the world
  ecs/drift.cpp          components and systems, for what there are thousands of
content/                 copied next to the plugin, readable as mod://<id>/...
```

Every `.cpp` under `src/` is part of the plugin, and each registers its own scripts, components and systems with a line at its bottom
(`SHARD_REGISTER_SCRIPT`, `SHARD_DEFINE_COMPONENT`, `SHARD_ON_LOAD`) : to add a feature, add files, and edit nothing else. Split your game in
folders as you like (`src/player/`, `src/enemies/`, `src/ui/`...).

The tutorial "Write a game with the SDK" (docs/docs/tutorials/write-a-game-with-the-sdk.md in the engine's repository) walks through it.

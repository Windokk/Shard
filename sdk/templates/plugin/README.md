# A Shard plugin

Built against the Shard SDK alone : no engine source, no engine library, any C++17 compiler.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=<the SDK folder>
cmake --build build
ShardHeadless --game <game library> --project <project.json> --plugin build/plugins/com.example.template --list-plugins
```

* `shard-plugin.json` : the manifest. Change `id` first (reverse DNS, lowercase), then `name`, `description`, `hosts`. The build adds `abi`, `linkage` and `binaries`.
* `src/plugin.cpp` : a component (`Spin`), a system that turns it every frame, a console variable (`<id>.scale`), the content folder read at mount, and the scale kept across a hot reload.
* `content/` : copied next to the plugin, readable as `mod://<id>/...`.

The tutorial "Write a plugin outside the engine repository" (docs/docs/tutorials.md in the engine's repository) walks through it.

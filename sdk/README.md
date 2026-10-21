# Shard SDK

The headers in `include/shard/` are Shard's C ABI : what a plugin built outside the engine includes. They compile as C11 and C++17, and nothing in them depends on the engine's compiler or standard library.

| Header | Content |
|---|---|
| `shard_abi.h` | versions (`SHARD_ABI_MAJOR/MINOR`), `ShardResult`, `ShardStr`, `ShardPluginCtx`, log levels |
| `shard_host.h` | `ShardHostApi` (what the host gives), `ShardPluginInfo` and `ShardPluginCallbacks` (what the plugin returns), `shard_plugin_entry` |
| `shard_plugin.h` | interface `shard.plugin` 1.0 : the plugin's id, version and folder |
| `shard_cvars.h` | interface `shard.cvars` 1.0 : variables the plugin owns (bool, int, float, string) |
| `shard_commands.h` | interface `shard.commands` 1.0 : console commands |

A plugin sets `"abi": "c1"` in its `shard-plugin.json` and exports `shard_plugin_entry`. See `docs/docs/concepts/plugins.md` (section "The C ABI") and `tests/plugin_fixtures/c_plugin/c_plugin.c` for a complete plugin in C. The plan and what is still to come (components, systems, events, a C++ wrapper, a CMake package) is in `docs/design-notes/c-abi-sdk.fr.md`.

## Rules for changing these headers

* A struct that crosses the boundary starts with `uint32_t structSize` and only ever grows at its end.
* A change to a struct or a table raises `SHARD_ABI_MINOR` (an incompatible one raises the major).
* `tests/plugins/abi_layout_tests.cpp` pins every layout : update it in the same change, deliberately.
* Fixed size types only inside a struct (`int32_t`, `uint64_t`), handles instead of pointers to host objects, no exception across the boundary.

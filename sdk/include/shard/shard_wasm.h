/* Sandboxed plugins (tier 2, "abi": "wasm1") : the C ABI, for a module built for wasm32 (docs/design-notes/wasm-sandbox.fr.md).
 * Experimental : it is not covered by the ABI's compatibility promise yet.
 *
 * A sandboxed plugin is the plugin you would write for the C ABI (shard.h, or shard/game.hpp in C++), compiled to WebAssembly. It can touch
 * nothing but its own memory and what the host gives it : no files, no network, no clock, no randomness of the system. It loads without a
 * signature or a consent, because it cannot do more than the host lets it : the interfaces below, filtered by the "permissions" of its
 * manifest. Its time (budgets.frameMs per frame) and its memory (budgets.memoryMB) are bounded ; a trap (a division by zero, an access out of
 * its memory, an `unreachable`), an exhausted budget or an import used wrongly stops the plugin, never the engine.
 *
 * The same source compiles for the engine and for wasm32. This header is all it takes : including it (shard.h does, when compiling for wasm)
 * declares every function of the interfaces as an import of the host and builds the tables (ShardCVarsApi...) from them. The ShardHostApi
 * that shard_plugin_entry receives is `shard_wasm_host_table`, which the module exports ; query_interface answers from the module's tables.
 * See shard_wasm_imports.h (generated from the other headers) for the list.
 *
 * What differs from a native plugin :
 *   - the interfaces : shard.plugin, commands, cvars, components, ecs, systems, events, state, scripts, actors, input, physics, audio, worlds,
 *     and shard.content for the plugin's own folder ("fs.read:content"). Not offered : shard.editor, hooks, services, extensions, experimental.
 *   - permissions : shard.content needs "fs.read:content", shard.input "input", shard.physics "physics", shard.audio "audio" (without it
 *     query_interface says SHARD_E_NOT_FOUND and the functions answer SHARD_E_UNSUPPORTED, or SHARD_FALSE / 0), worlds.load_world
 *     "worlds.load", actors.create / actors.spawn / worlds.spawn_prefab "actors.spawn".
 *   - memory of the host is copied : the block of a component that shard.ecs hands out, the bytes of a blob and the object of a script
 *     live in the module, and what the module changed is copied back when the call that made the copy returns (a system, a script's
 *     callback, a command...). A blob's pointer stays valid until the module resizes or frees the blob. A string the host hands out
 *     (shard.components string_get) is valid until the call returns.
 *   - the systems of a module run on the main thread, one after the other : a module is never entered by two threads at once.
 *   - ShardPluginCtx : whatever a module passes, an import acts for the plugin the module belongs to.
 *   - shard.content.read gives memory that is freed with ShardHostApi::free ; resolve and resolve_logical (which would give a path of the
 *     host) are always SHARD_E_UNSUPPORTED, and read / list only reach the plugin's own mod:// address.
 *   - the host's alloc and free are the module's memory (they count against budgets.memoryMB).
 *
 * Building one, in C without a libc :
 *   clang --target=wasm32 -ffreestanding -nostdlib -O2 -I<sdk>/include -c plugin.c -o plugin.o
 *   wasm-ld --no-entry --export=shard_plugin_entry --export=shard_wasm_host_table --export-table -o plugin.wasm plugin.o
 * ... and in C++ with wasi-sdk (a libc and a libc++ for wasm32, no exceptions) :
 *   clang++ --target=wasm32-wasi -fno-exceptions -DSHARD_SDK_NO_EXCEPTIONS -O2 -I<sdk>/include -c a.cpp b.cpp
 *   wasm-ld ... or clang++ -mexec-model=reactor -Wl,--export=shard_plugin_entry -Wl,--export=shard_wasm_host_table -Wl,--export-table
 * shard_sdk_add_plugin(... TARGET wasm) does that (sdk/cmake/ShardSDKPlugin.cmake).
 * The module must export its memory ("memory", wasm-ld's default) and its function table ("__indirect_function_table", --export-table) :
 * the host reads the arguments of the imports in the first and calls the callbacks through the second. A module with C++ constructors (or
 * a libc to start) exports `_initialize` (wasi-sdk reactors do) or `__wasm_call_ctors` : the host runs it before the entry point.
 * The module's allocator must get its memory from memory.grow, as wasi-libc's does : the host adds pages of its own after the module's. */
#ifndef SHARD_WASM_H
#define SHARD_WASM_H

#include "shard_abi.h"
#include "shard_host.h"
#include "shard_wasm_imports.h"

#endif

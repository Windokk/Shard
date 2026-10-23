/* Sandboxed plugins (tier 2, "abi": "wasm1") : what a WebAssembly module imports from the host (docs/design-notes/wasm-sandbox.fr.md).
 * Experimental : the list grows with each step of the note, and it is not covered by the ABI's compatibility promise yet.
 *
 * A sandboxed plugin is built for wasm32 and exports "shard_plugin_entry" like a native one ; the host passes 0 for `host` (the module has no
 * table of the host's functions : it calls the imports below) and the address of a ShardPluginInfo it reserved in the module's memory. The
 * callbacks are ordinary function pointers (indices in the module's table). The `ctx` a callback receives is the one to pass to the imports ;
 * the host ignores any other, an import always acts for the plugin of the sandbox. Everything is declared only when compiling for wasm.
 *
 * Building one, in C without a libc (the host runs it with Wasmtime) :
 *   clang --target=wasm32 -ffreestanding -nostdlib -O2 -I<sdk>/include -c plugin.c -o plugin.o
 *   wasm-ld --no-entry --export=shard_plugin_entry --export-table -o plugin.wasm plugin.o
 * The module must export its memory ("memory", wasm-ld's default) and its function table ("__indirect_function_table", --export-table) :
 * the host reads the arguments of the imports in the first and calls the callbacks through the second. */
#ifndef SHARD_WASM_H
#define SHARD_WASM_H

#include "shard_abi.h"
#include "shard_commands.h"
#include "shard_cvars.h"

#if defined(__wasm__)

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_WASM_IMPORT(name) __attribute__((import_module("shard"), import_name(name)))

/* The plugin's log, as ShardHostApi::log. A message longer than 64 KiB traps. */
SHARD_WASM_IMPORT("log") void shard_wasm_log(ShardPluginCtx ctx, uint32_t level, ShardStr message);

/* shard.cvars, as ShardCVarsApi : same rules, same results */
SHARD_WASM_IMPORT("cvars.create_int") ShardResult shard_wasm_cvar_create_int(ShardPluginCtx ctx, ShardStr name, int32_t value, ShardStr help, uint32_t flags, int32_t min, int32_t max, ShardCVar* out);
SHARD_WASM_IMPORT("cvars.create_float") ShardResult shard_wasm_cvar_create_float(ShardPluginCtx ctx, ShardStr name, float value, ShardStr help, uint32_t flags, float min, float max, ShardCVar* out);
SHARD_WASM_IMPORT("cvars.get_int") ShardResult shard_wasm_cvar_get_int(ShardPluginCtx ctx, ShardCVar cvar, int32_t* out);
SHARD_WASM_IMPORT("cvars.get_float") ShardResult shard_wasm_cvar_get_float(ShardPluginCtx ctx, ShardCVar cvar, float* out);
SHARD_WASM_IMPORT("cvars.set_int") ShardResult shard_wasm_cvar_set_int(ShardPluginCtx ctx, ShardCVar cvar, int32_t value);
SHARD_WASM_IMPORT("cvars.set_float") ShardResult shard_wasm_cvar_set_float(ShardPluginCtx ctx, ShardCVar cvar, float value);

/* shard.commands, as ShardCommandsApi::add. The words of the command and its output live in a page of the module's memory the host keeps
 * for itself : valid for the call only. The callback runs with the plugin's budget like any other. */
SHARD_WASM_IMPORT("commands.add") ShardResult shard_wasm_command_add(ShardPluginCtx ctx, ShardStr name, ShardStr help, ShardCommandFn run, void* user);

/* shard.content, permission "fs.read:content" (SHARD_E_UNSUPPORTED without it) : the file `path` of the plugin's own content folder (no
 * "..", nothing absolute : SHARD_E_INVALID_ARG). Copies up to `capacity` bytes into `buffer` and stores the whole size in *size. */
SHARD_WASM_IMPORT("content.read") ShardResult shard_wasm_content_read(ShardPluginCtx ctx, ShardStr path, void* buffer, uint64_t capacity, uint64_t* size);

#ifdef __cplusplus
}
#endif

#endif /* __wasm__ */

#endif

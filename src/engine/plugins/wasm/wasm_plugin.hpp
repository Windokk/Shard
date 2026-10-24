#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/containers/result.hpp"
#include "engine/plugins/context.hpp"
#include "engine/plugins/manifest.hpp"
#include "engine/plugins/plugin.hpp"

// A sandboxed (tier 2, abi "wasm1") plugin : docs/design-notes/wasm-sandbox.fr.md. The module is run by Wasmtime (wasm_runtime.hpp) ; the host
// builds a native ShardPluginInfo whose callbacks call into the sandbox, so the plugin then goes through the same IModule as a C plugin
// (CreateCPlugin), with the same registrars.
//
// What the module exports and imports (v1) :
//   export "memory" and "__indirect_function_table" (wasm-ld --export-table), "shard_plugin_entry" (i32 host, i32 info) -> i32 :
//     `host` is the address of the ShardHostApi the module exported as the global "shard_wasm_host_table" (sdk/include/shard/shard_wasm_imports.h
//     builds it from the imports), 0 for a module that has none ; `info` the address of a ShardPluginInfo laid out for wasm32 (pointers are 4
//     bytes), whose `callbacks` points at a wasm32 ShardPluginCallbacks : each callback is an index in the module's table, called as
//     (i32 user, i64 ctx) -> i32 (on_frame_begin / on_frame_end : (i32 user, i64 ctx, i64 frame) -> i32)
//   "_initialize" (reactor) or "__wasm_call_ctors", when present, runs before the entry point : the C++ constructors of a module built with wasi-sdk
//   the imports of module "shard" : one per function of the interfaces of the C ABI the sandbox offers (wasm_imports.hpp), and a few of
//   "wasi_snapshot_preview1" (wasm_wasi.cpp)

namespace Shard::Engine::Plugins {

    /// How many interpreter instructions one millisecond of a plugin's frame budget buys. Fixed, not measured, so that a run is the same everywhere.
    /// Calibrated in W6 (docs/design-notes/wasm-sandbox.fr.md section 9) on a Release build : the system of 10 000 entities burns 160 000 units and takes
    /// 0.046 ms with the copies of its columns, so a millisecond of the module is about 3.5 million units ; 3 million leaves a margin.
    inline constexpr uint64_t kWasmFuelPerMs = 3'000'000;
    /// The budget of a callback when the manifest gives none (budgets.frameMs)
    inline constexpr double kWasmDefaultFrameMs = 4.0;
    /// The memory of a sandbox when the manifest gives none (budgets.memoryMB)
    inline constexpr double kWasmDefaultMemoryMB = 64.0;

    /// @brief Decodes the module at `path`, instantiates it with the host's imports and calls its entry point. The result is an IModule like
    /// a C plugin's (GetCPluginFacets applies). A trap, an exhausted budget or a refused import fails the call it happens in, never the host.
    /// `cacheDirectory` : where the compiled module (Wasmtime's .cwasm) is kept so that the next load does not compile it again ; empty : no cache.
    Core::Result<IPlugin*> CreateWasmPlugin(PluginOwner owner, const PluginManifest& manifest, const std::string& path, const std::string& cacheDirectory = {});

    /// Drops the sandbox of a plugin whose instance is gone (its library slot is released)
    void ReleaseWasmPlugin(PluginOwner owner);

    /// The start of a frame : every sandbox gets its frame budget back. What a plugin runs during the frame (on_frame_begin, its systems, its
    /// scripts, its event handlers, on_frame_end) shares that budget ; the lifecycle callbacks and the console commands, which are not part of a
    /// frame, get a full budget each.
    void BeginWasmFrame();
    /// The end of the frame (after on_frame_end) : what runs next draws on a budget of its own
    void EndWasmFrame();

    /// Calls a function the module of a sandboxed plugin exports, as any other call into it : with its budget, its copies written back, a trap
    /// stopping the plugin. For the tests and the tools. False (and `error`) when the plugin has no such export or the call trapped.
    bool CallWasmExport(PluginOwner owner, const std::string& name, const std::vector<uint64_t>& args, std::vector<uint64_t>& results, std::string* error = nullptr);

    /// The bytes of the module's memory, for the tests and the tools (a string to hand an export, the answer of one that returns a pointer)
    bool WriteWasmMemory(PluginOwner owner, uint32_t offset, const void* data, uint32_t size);
    bool ReadWasmMemory(PluginOwner owner, uint32_t offset, void* out, uint32_t size);

    /// The fuel all the calls into a sandboxed plugin burnt so far (how many units of Wasmtime's fuel its code cost : the calibration of kWasmFuelPerMs)
    uint64_t WasmFuelUsed(PluginOwner owner);

    /// How often a compiled module came from the cache (for the tests)
    struct WasmCacheStats {
        uint64_t hits = 0, misses = 0, writes = 0;
    };
    WasmCacheStats GetWasmCacheStats();
}

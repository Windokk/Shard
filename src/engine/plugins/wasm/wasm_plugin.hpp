#pragma once

#include <cstdint>
#include <string>

#include "engine/core/containers/result.hpp"
#include "engine/plugins/context.hpp"
#include "engine/plugins/manifest.hpp"
#include "engine/plugins/plugin.hpp"

// A sandboxed (tier 2, abi "wasm1") plugin : docs/design-notes/wasm-sandbox.fr.md. The module is run by the interpreter of wasm_runtime.hpp ;
// the host builds a native ShardPluginInfo whose callbacks call into the sandbox, so the plugin then goes through the same IModule as a C
// plugin (CreateCPlugin), with the same registrars.
//
// What the module exports and imports (v1) :
//   export "memory", export "shard_plugin_entry" (i32 host, i32 info) -> i32 : `host` is 0 (the sandbox reaches the host through its imports),
//     `info` the address of a ShardPluginInfo laid out for wasm32 (pointers are 4 bytes), whose `callbacks` points at a wasm32
//     ShardPluginCallbacks : each callback is an index in the module's table, called as (i32 user, i64 ctx) -> i32
//     (on_frame_begin / on_frame_end : (i32 user, i64 ctx, i64 frame) -> i32)
//   import "shard" "log" (i64 ctx, i32 level, i32 message) : `message` is the address of a wasm32 ShardStr

namespace Shard::Engine::Plugins {

    /// How many interpreter instructions one millisecond of a plugin's frame budget buys. Fixed, not measured, so that a run is the same everywhere.
    inline constexpr uint64_t kWasmFuelPerMs = 1'000'000;
    /// The budget of a callback when the manifest gives none (budgets.frameMs)
    inline constexpr double kWasmDefaultFrameMs = 4.0;
    /// The memory of a sandbox when the manifest gives none (budgets.memoryMB)
    inline constexpr double kWasmDefaultMemoryMB = 64.0;

    /// @brief Decodes the module at `path`, instantiates it with the host's imports and calls its entry point. The result is an IModule like
    /// a C plugin's (GetCPluginFacets applies). A trap, an exhausted budget or a refused import fails the call it happens in, never the host.
    Core::Result<IPlugin*> CreateWasmPlugin(PluginOwner owner, const PluginManifest& manifest, const std::string& path);

    /// Drops the sandbox of a plugin whose instance is gone (its library slot is released)
    void ReleaseWasmPlugin(PluginOwner owner);

    /// The start of a frame : every sandbox gets its frame budget back. What a plugin runs during the frame (on_frame_begin, on_frame_end)
    /// shares that budget ; the lifecycle callbacks and the console commands, which are not part of a frame, get a full budget each.
    void BeginWasmFrame();
}

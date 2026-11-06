#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/containers/result.hpp"
#include "engine/plugins/context.hpp"
#include "engine/plugins/manifest.hpp"
#include "engine/plugins/plugin.hpp"

// Sandboxed (tier 2, abi "wasm1") plugin run by Wasmtime. The host builds a native ShardPluginInfo whose callbacks call into the sandbox, so it goes through the same IModule as a C plugin.
// Exports: "memory", "__indirect_function_table", "shard_plugin_entry"(i32 host, i32 info) -> i32. Callbacks are table indices called as (i32 user, i64 ctx) -> i32. "_initialize" or "__wasm_call_ctors" runs first. Imports: module "shard" plus a few "wasi_snapshot_preview1".

namespace Shard::Engine::Plugins {

    // Interpreter instructions bought by 1 ms of frame budget. Fixed, not measured, so runs match everywhere. Calibrated on Release: a 10 000 entity system burns 160 000 units in 0.046 ms, so 1 ms is about 3.5M units, 3M leaves margin.
    inline constexpr uint64_t kWasmFuelPerMs = 3'000'000;
    inline constexpr double kWasmDefaultFrameMs = 4.0;
    inline constexpr double kWasmDefaultMemoryMB = 64.0;

    Core::Result<IPlugin*> CreateWasmPlugin(PluginOwner owner, const PluginManifest& manifest, const std::string& path, const std::string& cacheDirectory = {});

    void ReleaseWasmPlugin(PluginOwner owner);

    void BeginWasmFrame();
    void EndWasmFrame();

    bool CallWasmExport(PluginOwner owner, const std::string& name, const std::vector<uint64_t>& args, std::vector<uint64_t>& results, std::string* error = nullptr);

    bool WriteWasmMemory(PluginOwner owner, uint32_t offset, const void* data, uint32_t size);
    bool ReadWasmMemory(PluginOwner owner, uint32_t offset, void* out, uint32_t size);

    uint64_t WasmFuelUsed(PluginOwner owner);

    struct WasmCacheStats {
        uint64_t hits = 0, misses = 0, writes = 0;
    };
    WasmCacheStats GetWasmCacheStats();
}

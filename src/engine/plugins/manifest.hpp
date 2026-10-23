#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/core/containers/result.hpp"
#include "engine/plugins/version.hpp"

namespace Shard::Engine::Plugins {

    /// The API version plugins are built against. Within a major, functions are only added.
    inline constexpr uint32_t kPluginApiMajor = 1;
    inline constexpr uint32_t kPluginApiMinor = 0;

    /// @brief The executables a plugin can live in. A manifest lists several ; the running host only loads the plugins that name it.
    enum class Host : uint8_t {
        Engine = 1,         ///< any executable that runs the engine : listing only this one means "everywhere but tools"
        Editor = 2,
        Player = 4,
        Headless = 8,       ///< ShardHeadless, dedicated servers
        Tool = 16,          ///< cooker, baker, asset validator
    };
    constexpr uint8_t operator|(Host a, Host b) { return uint8_t(a) | uint8_t(b); }
    constexpr uint8_t operator|(uint8_t a, Host b) { return a | uint8_t(b); }
    constexpr uint8_t HostBit(Host h) { return uint8_t(h); }
    const char* HostName(Host host);
    bool ParseHost(const std::string& name, Host& out);
    /// "engine" is accepted in a manifest and means Player + Headless + Editor (every executable that runs a simulation)
    inline constexpr uint8_t kAllSimulationHosts = uint8_t(Host::Editor) | uint8_t(Host::Player) | uint8_t(Host::Headless);

    enum class PluginKind : uint8_t { Plugin, Module };
    const char* PluginKindName(PluginKind kind);

    enum class ModuleKind : uint8_t {
        None,           ///< not a module
        Engine,         ///< a part of the engine itself (physics, audio...), linked statically
        Genre,          ///< a reusable toolkit : voxel, planetary, racing...
        Code,           ///< a game's own code
        Mod,            ///< content and logic from someone else
        Datapack,       ///< data only
    };
    const char* ModuleKindName(ModuleKind kind);

    enum class Linkage : uint8_t { Dynamic, Static };

    /// How much the plugin is trusted (see the docs, "Trust levels")
    enum class TrustTier : uint8_t { FirstParty = 0, Native = 1, Sandboxed = 2, Data = 3 };

    struct DependencySpec {
        std::string id;
        VersionRange range = VersionRange::Any();
        bool optional = false;
    };

    struct ProvidedService {
        std::string service;
        SemVer version;
    };

    /// @brief What is in a plugin's shard-plugin.json, checked.
    struct PluginManifest {
        uint32_t schema = 1;
        std::string id;
        std::string name;
        std::string description;
        std::vector<std::string> authors;
        SemVer version;

        PluginKind kind = PluginKind::Plugin;
        ModuleKind moduleKind = ModuleKind::None;
        Linkage linkage = Linkage::Dynamic;
        TrustTier tier = TrustTier::FirstParty;
        uint8_t hosts = kAllSimulationHosts;

        uint32_t apiMajor = kPluginApiMajor;
        uint32_t apiMinor = 0;
        VersionRange engineVersions = VersionRange::Any();
        std::vector<std::string> engineRequires;        ///< features of the engine : largeWorld, determinism...
        std::string abiFingerprint;                      ///< empty : not checked (a datapack, a static module)

        /// "cpp" : the C++ descriptor (shard_plugin_describe), checked with the full build fingerprint ; "c1" : the C ABI of sdk/include/shard
        /// (shard_plugin_entry), checked with the ABI version only ; "wasm1" : a WebAssembly module run in the sandbox (binaries["wasm"],
        /// docs/design-notes/wasm-sandbox.fr.md), the only kind a sandboxed (tier 2) plugin can be
        std::string abi = "cpp";

        std::map<std::string, std::string> binaries;    ///< platform key -> path relative to the plugin folder
        std::string entry = "shard_plugin_describe";    ///< "shard_plugin_entry" when abi is "c1" and the manifest says nothing

        std::vector<DependencySpec> dependencies;
        std::vector<DependencySpec> conflicts;
        std::vector<ProvidedService> provides;
        std::vector<std::string> loadAfter;
        std::vector<std::string> loadBefore;

        bool affectsSimulation = false;
        bool deterministic = true;

        std::string contentMount = "content";           ///< folder (relative to the plugin) mounted as mod://<id>/ ; empty : no content
        std::string assetNamespace;                      ///< defaults to the id
        std::vector<std::string> overrides;              ///< asset paths of lower layers this plugin may replace ("*" : any)

        double budgetMemoryMB = 0.0;                     ///< 0 : no budget
        double budgetFrameMs = 0.0;
        bool enforceBudgets = false;                     ///< memory : an allocation through PluginContext::Memory() that would pass the budget fails
        std::vector<std::string> permissions;

        nlohmann::json settings = nlohmann::json::object();     ///< defaults for the plugin's cvars (key : name without the id prefix)

        std::map<std::string, std::string> tags;         ///< the tags the plugin declares : name -> comment (see docs/concepts/tags.md)

        // ---- Filled by discovery, not read from the file ----
        std::string rootDir;                              ///< folder the manifest was found in (absolute, '/' separators)
        std::string manifestPath;

        bool RunsOn(Host host) const { return (hosts & HostBit(host)) != 0; }
        bool IsModule() const { return kind == PluginKind::Module; }
        bool IsDatapack() const { return moduleKind == ModuleKind::Datapack; }
        bool HasContent() const { return !contentMount.empty(); }
        /// The asset namespace, or the id
        const std::string& Namespace() const { return assetNamespace.empty() ? id : assetNamespace; }
        /// The binary for this machine (relative path), or "" when the manifest has none
        std::string BinaryForThisPlatform() const;
    };

    /// "linux-x86_64", "windows-x86_64", "macos-arm64"...
    std::string CurrentPlatformKey();

    /// Plugin ids : lowercase letters, digits, '-' and '_', in dot separated segments ("com.acme.weather")
    bool IsValidPluginId(const std::string& id);

    /// @brief Parses and validates a manifest. All the problems found are reported in one message, one per line.
    Core::Result<PluginManifest> ParseManifest(const nlohmann::json& json);
    Core::Result<PluginManifest> ParseManifestText(const std::string& text);
    nlohmann::ordered_json ManifestToJson(const PluginManifest& manifest);
}

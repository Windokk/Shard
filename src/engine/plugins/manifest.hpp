#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/core/containers/result.hpp"
#include "engine/plugins/version.hpp"

namespace Shard::Engine::Plugins {

    // Within a major, functions are only added
    inline constexpr uint32_t kPluginApiMajor = 1;
    inline constexpr uint32_t kPluginApiMinor = 0;

    enum class Host : uint8_t {
        Engine = 1,
        Editor = 2,
        Player = 4,
        Headless = 8,
        Tool = 16,
    };
    constexpr uint8_t operator|(Host a, Host b) { return uint8_t(a) | uint8_t(b); }
    constexpr uint8_t operator|(uint8_t a, Host b) { return a | uint8_t(b); }
    constexpr uint8_t HostBit(Host h) { return uint8_t(h); }
    const char* HostName(Host host);
    bool ParseHost(const std::string& name, Host& out);
    inline constexpr uint8_t kAllSimulationHosts = uint8_t(Host::Editor) | uint8_t(Host::Player) | uint8_t(Host::Headless);

    enum class PluginKind : uint8_t { Plugin, Module };
    const char* PluginKindName(PluginKind kind);

    enum class ModuleKind : uint8_t {
        None,
        Engine,
        Genre,
        Code,
        Mod,
        Datapack,
    };
    const char* ModuleKindName(ModuleKind kind);

    enum class Linkage : uint8_t { Dynamic, Static };

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
        std::vector<std::string> engineRequires;
        std::string abiFingerprint;

        // "cpp": C++ descriptor (shard_plugin_describe), checked with the full build fingerprint.
        // "c1": C ABI (shard_plugin_entry), checked by ABI version only. "wasm1": sandboxed module (binaries["wasm"]), the only kind tier 2 can be.
        std::string abi = "cpp";

        std::map<std::string, std::string> binaries;
        std::string entry = "shard_plugin_describe";

        std::vector<DependencySpec> dependencies;
        std::vector<DependencySpec> conflicts;
        std::vector<ProvidedService> provides;
        std::vector<std::string> loadAfter;
        std::vector<std::string> loadBefore;

        bool affectsSimulation = false;
        bool deterministic = true;

        std::string contentMount = "content";
        std::string assetNamespace;
        std::vector<std::string> overrides;

        double budgetMemoryMB = 0.0;
        double budgetFrameMs = 0.0;
        bool enforceBudgets = false;
        std::vector<std::string> permissions;

        nlohmann::json settings = nlohmann::json::object();

        std::map<std::string, std::string> tags;

        std::string rootDir;
        std::string manifestPath;

        bool RunsOn(Host host) const { return (hosts & HostBit(host)) != 0; }
        bool IsModule() const { return kind == PluginKind::Module; }
        bool IsDatapack() const { return moduleKind == ModuleKind::Datapack; }
        bool HasContent() const { return !contentMount.empty(); }
        const std::string& Namespace() const { return assetNamespace.empty() ? id : assetNamespace; }
        std::string BinaryForThisPlatform() const;
    };

    std::string CurrentPlatformKey();

    bool IsValidPluginId(const std::string& id);

    Core::Result<PluginManifest> ParseManifest(const nlohmann::json& json);
    Core::Result<PluginManifest> ParseManifestText(const std::string& text);
    nlohmann::ordered_json ManifestToJson(const PluginManifest& manifest);
}

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/core/containers/result.hpp"
#include "engine/plugins/manifest.hpp"
#include "engine/plugins/package.hpp"
#include "engine/plugins/version.hpp"

namespace Shard::Engine::Plugins {

    struct ParsedUrl {
        std::string scheme;
        std::string host;
        uint16_t port = 0;
        std::string path;
    };
    std::optional<ParsedUrl> ParseUrl(const std::string& url);

    // https:// is refused, the engine has no TLS. Index and archives are verified by hash and signature anyway.
    Core::Result<std::vector<uint8_t>> FetchBytes(const std::string& source, size_t maxBytes = 512u * 1024 * 1024, int timeoutMs = 10000);

    struct RegistryVersion {
        std::string id;
        std::string name;
        std::string description;
        SemVer version;
        std::string file;
        std::string sha256;
        uint64_t size = 0;
        std::string publisher;
        VersionRange engineVersions = VersionRange::Any();
        std::vector<DependencySpec> dependencies;
        std::string registry;
    };

    // index.json: { schema, name, plugins: [ { id, name, description, versions: [ { version, file, sha256, size, publisher, engine, dependencies } ] } ] }
    class Registry {
    public:
        Core::Result<bool> Load(const std::string& source);
        Core::Result<bool> LoadFromJson(const nlohmann::json& index, const std::string& source);

        const std::string& Source() const { return m_Source; }
        const std::string& Name() const { return m_Name; }
        const std::vector<RegistryVersion>& Versions() const { return m_Versions; }

        std::vector<RegistryVersion> Search(const std::string& text) const;
        std::optional<RegistryVersion> Find(const std::string& id, const VersionRange& range, const SemVer& engine) const;

        std::string ArchiveLocation(const RegistryVersion& version) const;

    private:
        std::string m_Source;
        std::string m_Name;
        std::vector<RegistryVersion> m_Versions;
    };

    struct InstallOptions {
        std::string installDirectory;
        SemVer engineVersion;
        const TrustStore* trust = nullptr;
        bool allowUnsigned = false;
        std::vector<std::string> alreadyAvailable;
    };

    struct InstalledPlugin {
        std::string id;
        std::string version;
        std::string folder;
        std::string source;
        std::string publisher;
    };

    // Download, check size and SHA-256 against the index, unpack, verify signature, then move into place. Writes .shard-install.json, after which the plugin must carry a trusted signature to load.
    class PluginInstaller {
    public:
        explicit PluginInstaller(std::vector<Registry> registries) : m_Registries(std::move(registries)) {}

        Core::Result<std::vector<RegistryVersion>> Plan(const std::string& id, const VersionRange& range, const InstallOptions& options) const;
        Core::Result<std::vector<InstalledPlugin>> Install(const std::string& id, const VersionRange& range, const InstallOptions& options) const;

        static std::vector<InstalledPlugin> ListInstalled(const std::string& installDirectory);
        static Core::Result<bool> Uninstall(const std::string& installDirectory, const std::string& id);
        std::vector<std::pair<InstalledPlugin, RegistryVersion>> FindUpdates(const std::string& installDirectory, const SemVer& engineVersion) const;

        const std::vector<Registry>& Registries() const { return m_Registries; }

    private:
        Core::Result<InstalledPlugin> InstallOne(const RegistryVersion& version, const InstallOptions& options) const;
        std::vector<Registry> m_Registries;
    };
}

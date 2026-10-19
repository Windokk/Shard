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
        std::string scheme;         ///< "http" or "https"
        std::string host;
        uint16_t port = 0;
        std::string path;           ///< with the query, starts with '/'
    };
    std::optional<ParsedUrl> ParseUrl(const std::string& url);

    /// @brief Fetches a registry document : a file (a path, or file://), or an `http://` URL (plain HTTP/1.0 over TCP ; redirects are followed
    /// within http). `https://` is refused : nothing in the engine speaks TLS, and the index and the archives are checked by hash and signature anyway.
    Core::Result<std::vector<uint8_t>> FetchBytes(const std::string& source, size_t maxBytes = 512u * 1024 * 1024, int timeoutMs = 10000);

    /// One downloadable version of a plugin
    struct RegistryVersion {
        std::string id;
        std::string name;
        std::string description;
        SemVer version;
        std::string file;               ///< the archive, relative to the index (or an absolute URL)
        std::string sha256;
        uint64_t size = 0;
        std::string publisher;
        VersionRange engineVersions = VersionRange::Any();
        std::vector<DependencySpec> dependencies;
        std::string registry;           ///< where the index was read (set by Registry::Load)
    };

    /// @brief A plugin registry : an `index.json` and the archives it names.
    ///
    ///   { "schema": 1, "name": "Studio registry", "plugins": [ { "id": "com.acme.weather", "name": "...", "description": "...",
    ///       "versions": [ { "version": "1.4.2", "file": "com.acme.weather-1.4.2.shardplugin", "sha256": "<hex>", "size": 12345,
    ///                       "publisher": "acme", "engine": ">=0.1", "dependencies": [ { "id": "shard.audio", "version": "^1.0" } ] } ] } ] }
    class Registry {
    public:
        /// `source` : the index file, a folder holding index.json, or an http:// URL of the index
        Core::Result<bool> Load(const std::string& source);
        Core::Result<bool> LoadFromJson(const nlohmann::json& index, const std::string& source);

        const std::string& Source() const { return m_Source; }
        const std::string& Name() const { return m_Name; }
        const std::vector<RegistryVersion>& Versions() const { return m_Versions; }

        /// Plugins whose id, name or description contains `text` (case does not matter), one entry per plugin (its highest version)
        std::vector<RegistryVersion> Search(const std::string& text) const;
        /// The highest version of `id` inside `range` that works with `engine`
        std::optional<RegistryVersion> Find(const std::string& id, const VersionRange& range, const SemVer& engine) const;

        /// Where the archive of a version is
        std::string ArchiveLocation(const RegistryVersion& version) const;

    private:
        std::string m_Source;
        std::string m_Name;
        std::vector<RegistryVersion> m_Versions;
    };

    /// What an install needs to know
    struct InstallOptions {
        std::string installDirectory;           ///< the plugins folder : <dir>/<id>/ is where a plugin goes
        SemVer engineVersion;
        const TrustStore* trust = nullptr;
        bool allowUnsigned = false;             ///< install a plugin even if it carries no valid signature from a trusted publisher
        std::vector<std::string> alreadyAvailable;     ///< ids that need not be installed (the engine's own modules)
    };

    struct InstalledPlugin {
        std::string id;
        std::string version;
        std::string folder;
        std::string source;
        std::string publisher;
    };

    /// @brief Installs plugins from registries : downloads the archive, checks its size and SHA-256 against the index, unpacks it, checks the
    /// signature of the files, and only then puts the folder in place (an existing version is replaced). Dependencies the registries can
    /// satisfy are installed first. Writes `.shard-install.json` in the folder : from then on the plugin must carry a valid signature of a trusted
    /// publisher to be loaded.
    class PluginInstaller {
    public:
        explicit PluginInstaller(std::vector<Registry> registries) : m_Registries(std::move(registries)) {}

        /// What would be installed, dependencies first
        Core::Result<std::vector<RegistryVersion>> Plan(const std::string& id, const VersionRange& range, const InstallOptions& options) const;
        /// @return the plugins installed, dependencies first
        Core::Result<std::vector<InstalledPlugin>> Install(const std::string& id, const VersionRange& range, const InstallOptions& options) const;

        static std::vector<InstalledPlugin> ListInstalled(const std::string& installDirectory);
        static Core::Result<bool> Uninstall(const std::string& installDirectory, const std::string& id);
        /// Installed plugins for which a registry has a newer version inside the same major
        std::vector<std::pair<InstalledPlugin, RegistryVersion>> FindUpdates(const std::string& installDirectory, const SemVer& engineVersion) const;

        const std::vector<Registry>& Registries() const { return m_Registries; }

    private:
        Core::Result<InstalledPlugin> InstallOne(const RegistryVersion& version, const InstallOptions& options) const;
        std::vector<Registry> m_Registries;
    };
}

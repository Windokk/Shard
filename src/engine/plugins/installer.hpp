#pragma once

#include <string>
#include <vector>

#include "engine/core/containers/result.hpp"
#include "engine/plugins/manifest.hpp"
#include "engine/plugins/package.hpp"
#include "engine/plugins/version.hpp"

namespace Shard::Engine::Plugins {

    struct InstallOptions {
        std::string installDirectory;
        SemVer engineVersion;
        const TrustStore* trust = nullptr;
        bool allowUnsigned = false;
        std::vector<std::string> alreadyAvailable;
        std::string expectedSha256;
    };

    struct InstalledPlugin {
        std::string id;
        std::string version;
        std::string folder;
        std::string source;
        std::string publisher;
    };

    struct BundleEntry {
        std::string id;
        std::string name;
        SemVer version;
        std::string location;
        std::string archiveEntry;
        std::vector<DependencySpec> dependencies;
    };

    class PluginInstaller {
    public:
        static Core::Result<InstalledPlugin> InstallArchive(const std::string& archivePath, const InstallOptions& options);

        static Core::Result<std::vector<BundleEntry>> ReadBundle(const std::string& bundlePath, const InstallOptions& options);
        static Core::Result<std::vector<BundleEntry>> PlanBundle(const std::string& bundlePath, const std::vector<std::string>& only, const InstallOptions& options);
        static Core::Result<std::vector<InstalledPlugin>> InstallBundle(const std::string& bundlePath, const std::vector<std::string>& only, const InstallOptions& options);

        static std::vector<InstalledPlugin> ListInstalled(const std::string& installDirectory);
        static Core::Result<bool> Uninstall(const std::string& installDirectory, const std::string& id);
    };
}

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Shard::Engine::Filesystem {

    struct ImportResult {
        bool ok = false;
        std::string message;
        std::vector<std::string> outputs;
    };

    struct AssetImporterExtension {
        std::string name;
        std::vector<std::string> extensions;
        std::function<ImportResult(const std::string& sourcePath, const std::string& outputDirectory)> import;
    };

    class AssetImporters {
    public:
        static AssetImporters& Get();

        void Register(std::shared_ptr<AssetImporterExtension> importer);
        void Unregister(const std::shared_ptr<AssetImporterExtension>& importer);

        std::shared_ptr<AssetImporterExtension> Find(const std::string& path) const;
        bool CanImport(const std::string& path) const { return Find(path) != nullptr; }

        ImportResult Import(const std::string& sourcePath, const std::string& outputDirectory) const;

        std::vector<std::string> Extensions() const;

    private:
        mutable std::mutex m_Mutex;
        std::vector<std::shared_ptr<AssetImporterExtension>> m_Importers;
    };
}

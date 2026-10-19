#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Shard::Engine::Filesystem {

    /// What an importer made
    struct ImportResult {
        bool ok = false;
        std::string message;                        ///< why it failed, or a note on what it did
        std::vector<std::string> outputs;           ///< the files it wrote (absolute paths), which the project can then index
    };

    /// @brief A converter from a source format the engine doesn't read to files it does : the "asset.importer" extension point.
    struct AssetImporterExtension {
        std::string name;
        std::vector<std::string> extensions;        ///< the source extensions it takes (".foo", compared without case)
        /// Converts `sourcePath` into files in `outputDirectory` (created by the caller)
        std::function<ImportResult(const std::string& sourcePath, const std::string& outputDirectory)> import;
    };

    /// @brief The importers that are registered (by the engine from the plugins' extension items). Thread safe.
    class AssetImporters {
    public:
        static AssetImporters& Get();

        void Register(std::shared_ptr<AssetImporterExtension> importer);
        void Unregister(const std::shared_ptr<AssetImporterExtension>& importer);

        /// The importer that takes this file's extension (the first registered), null if none
        std::shared_ptr<AssetImporterExtension> Find(const std::string& path) const;
        bool CanImport(const std::string& path) const { return Find(path) != nullptr; }

        /// Runs the importer of `sourcePath` ; ok = false and a message if there is none, or the source is missing, or it threw
        ImportResult Import(const std::string& sourcePath, const std::string& outputDirectory) const;

        std::vector<std::string> Extensions() const;

    private:
        mutable std::mutex m_Mutex;
        std::vector<std::shared_ptr<AssetImporterExtension>> m_Importers;
    };
}

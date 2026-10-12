#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/assets/assetID.hpp"
#include "engine/assets/project/project.hpp"
#include "engine/assets/vfs/filesystem.hpp"
#include "engine/platform/filesystem/file_watcher.hpp"

namespace Shard::Engine::Core::Resources { class ResourcesManager; }

namespace Shard::Engine::Assets {

    /// @brief Keeps the running engine in line with the asset files on disk, whoever changes them (an image editor, a text
    /// editor, a file explorer, git...). The OS tells which file changed (FileWatcher) ; the changes are gathered until the
    /// file is quiet and readable, then applied from Update(), on the main thread :
    ///  - a modified texture, shader (and the .glsl they include), compute shader or material that is loaded is rebuilt
    ///    in place : the object, its AssetID and every pointer to it stay, only its content (GL texture, program, parameters) changes.
    ///    The materials that use a reloaded texture/shader are re-read too. A shader that fails to compile keeps its previous program.
    ///  - a new file is registered in the asset database (it gets an AssetID, like Sync in the asset browser)
    ///  - a renamed/moved file or folder keeps its AssetID ; the worlds and materials that name it and the build settings follow
    ///  - a deleted file or folder is unregistered, unless a loaded world still uses it (it stays until it is released)
    /// The engine's own resources (engine_resources) are watched for modifications only.
    class HotReloader {
    public:
        struct Config {
            Filesystem::FileManager* files = nullptr;
            Filesystem::AssetIDManager* ids = nullptr;
            Core::Resources::ResourcesManager* resources = nullptr;
            Projects::BuildSettings* buildSettings = nullptr;
            /// Saves the asset database. Empty when the project is read only.
            std::function<void()> persistDatabase;
        };

        HotReloader();
        ~HotReloader();
        HotReloader(const HotReloader&) = delete;
        HotReloader& operator=(const HotReloader&) = delete;

        /// @brief Starts watching the project's resources and the engine's. False if neither could be watched.
        bool Start(const Config& config);
        void Stop();
        bool IsRunning() const { return m_Running; }

        /// @brief Applies the changes that have settled. Call it once a frame, on the main thread.
        void Update();

        /// @brief Bumped each time the set of assets (not their content) changed : a UI listing them knows to refresh.
        uint64_t GetRevision() const { return m_Revision; }

        /// @brief How long a file must be left alone before its change is applied (default 0.25 s : editors write in several steps).
        void SetSettleTime(double seconds) { m_SettleSeconds = seconds; }

    private:
        struct Root {
            std::string path;
            bool isEngine = false;
            std::unique_ptr<Core::Platform::FileWatcher> watcher;
        };

        struct Entry {
            Core::Platform::FileChangeKind kind;
            std::string path;
            std::string oldPath;                // Renamed only
            bool engine = false;
            std::chrono::steady_clock::time_point last;
            int attempts = 0;
        };

        void Ingest(const Core::Platform::FileChange& change, bool engine);
        bool Apply(Entry& entry);

        void ApplyCreated(const std::string& path);
        void ApplyModified(const std::string& path, bool engine);
        void ApplyDeleted(const std::string& path);
        void ApplyRenamed(const std::string& oldPath, const std::string& newPath);

        /// Registers `path` (a file, or every file under a folder). Returns the number of new assets.
        int RegisterPath(const std::string& path);
        void ReloadByFile(const std::string& nameInProject);
        std::string NameOf(const std::string& path) const;

        Config m_Config;
        std::vector<Root> m_Roots;
        std::vector<Entry> m_Entries;
        bool m_Running = false;
        double m_SettleSeconds = 0.25;
        uint64_t m_Revision = 0;
        bool m_DatabaseDirty = false;
    };
}

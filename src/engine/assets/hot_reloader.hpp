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
    ///  - a modified mesh (.fbx) is read again and replaces the cached one ; the models that use it are pointed at the new one
    ///    (onMeshReplaced). A modified world, or a probe bake / lightmap file the loaded world uses, reloads that world (onWorldChanged).
    /// What the engine itself writes (a world saved, a bake) is not taken for an outside change : see NoteOwnWrite.
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
            /// A mesh was replaced in the cache by a freshly read one : whatever points at the old one (models) has to follow.
            std::function<void(const std::string& nameInProject)> onMeshReplaced;
            /// A world file, or a file it uses that is only read when it loads (bake, lightmap), changed : reload it if it is loaded.
            std::function<void(const std::string& nameInProject)> onWorldChanged;
        };

        /// Told (on the main thread, from Update) of an asset whose content changed on disk, and of the assets that depend on
        /// it (a texture's materials) : whatever derives something from them (thumbnails) is out of date.
        using ChangeListener = std::function<void(const std::string& nameInProject)>;
        int AddChangeListener(ChangeListener listener);
        void RemoveChangeListener(int id);

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

        void NotifyChanged(const std::string& nameInProject);

        Config m_Config;
        std::vector<std::pair<int, ChangeListener>> m_Listeners;
        int m_NextListenerID = 1;
        std::vector<Root> m_Roots;
        std::vector<Entry> m_Entries;
        bool m_Running = false;
        double m_SettleSeconds = 0.25;
        uint64_t m_Revision = 0;
        bool m_DatabaseDirty = false;
    };

    /// @brief Tells the hot reload that this process just wrote `path` itself (a world saved, a bake...) : the change events
    /// the OS is about to send for it are not an outside edit and are ignored, as long as the file stays as it was written.
    /// Call it once the file is closed. Safe from any thread.
    void NoteOwnWrite(const std::string& path);
}

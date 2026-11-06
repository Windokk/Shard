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

    // Keeps the running engine in sync with asset files on disk. Changes are gathered until the file is quiet and readable, then applied from Update() on the main thread.
    // Reloads are in place, so AssetID and pointers stay valid. A shader that fails to compile keeps its old program.
    class HotReloader {
    public:
        struct Config {
            Filesystem::FileManager* files = nullptr;
            Filesystem::AssetIDManager* ids = nullptr;
            Core::Resources::ResourcesManager* resources = nullptr;
            std::function<void(const std::string& oldName, const std::string& newName, bool isDirectory)> remapWorlds;
            std::function<void()> persistDatabase;
            std::function<void(const std::string& nameInProject)> onMeshReplaced;
            std::function<void(const std::string& nameInProject)> onWorldChanged;
        };

        using ChangeListener = std::function<void(const std::string& nameInProject)>;
        int AddChangeListener(ChangeListener listener);
        void RemoveChangeListener(int id);

        HotReloader();
        ~HotReloader();
        HotReloader(const HotReloader&) = delete;
        HotReloader& operator=(const HotReloader&) = delete;

        bool Start(const Config& config);
        void Stop();
        bool IsRunning() const { return m_Running; }

        void Update();

        uint64_t GetRevision() const { return m_Revision; }

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
            std::string oldPath;
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

    // Call after closing the file, from any thread. OS events for our own writes are ignored while the file stays as written.
    void NoteOwnWrite(const std::string& path);
}

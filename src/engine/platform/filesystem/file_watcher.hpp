#pragma once

#include <memory>
#include <string>
#include <vector>

namespace Shard::Engine::Core::Platform {

    enum class FileChangeKind { Created, Modified, Deleted, Renamed };

    struct FileChange {
        FileChangeKind kind = FileChangeKind::Modified;
        std::string path;           // absolute, forward slashes
        std::string oldPath;        // Renamed only
    };

    /// @brief Watches a directory tree with the OS notifications (ReadDirectoryChangesW / inotify) : no polling of
    /// the files. The changes are queued by a background thread and picked up with Poll(), from the thread that
    /// reacts to them (the asset database refresh, the hot reload of the game module).
    class FileWatcher {
    public:
        FileWatcher();
        ~FileWatcher();
        FileWatcher(const FileWatcher&) = delete;
        FileWatcher& operator=(const FileWatcher&) = delete;

        /// @brief Starts watching. A watcher watches one directory : Start again to change it.
        bool Start(const std::string& directory, bool recursive = true);
        void Stop();
        bool IsWatching() const;

        /// @brief The changes since the last call, in the order they happened.
        std::vector<FileChange> Poll();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}

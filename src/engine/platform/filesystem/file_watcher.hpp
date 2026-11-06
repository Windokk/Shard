#pragma once

#include <memory>
#include <string>
#include <vector>

namespace Shard::Engine::Core::Platform {

    enum class FileChangeKind { Created, Modified, Deleted, Renamed };

    struct FileChange {
        FileChangeKind kind = FileChangeKind::Modified;
        std::string path;
        std::string oldPath;
    };

    class FileWatcher {
    public:
        FileWatcher();
        ~FileWatcher();
        FileWatcher(const FileWatcher&) = delete;
        FileWatcher& operator=(const FileWatcher&) = delete;

        bool Start(const std::string& directory, bool recursive = true);
        void Stop();
        bool IsWatching() const;

        std::vector<FileChange> Poll();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}

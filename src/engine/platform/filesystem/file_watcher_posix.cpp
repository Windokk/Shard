#if !defined(_WIN32)

#include "engine/platform/filesystem/file_watcher.hpp"
#include "engine/platform/filesystem/paths.hpp"

#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>

#if defined(__linux__)
    #include <sys/inotify.h>
#endif

namespace Shard::Engine::Core::Platform {

#if defined(__linux__)

    struct FileWatcher::Impl {
        int inotifyFd = -1;
        int wakePipe[2] = { -1, -1 };
        std::thread thread;
        std::string root;
        bool recursive = true;

        std::unordered_map<int, std::string> watches;   // watch descriptor -> directory (only touched by Run and Start)

        std::mutex mutex;
        std::vector<FileChange> queue;
        std::atomic<bool> running{ false };

        void Push(FileChange change)
        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.push_back(std::move(change));
        }

        void AddWatch(const std::string& directory)
        {
            const int wd = inotify_add_watch(inotifyFd, directory.c_str(),
                IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF);
            if (wd >= 0)
                watches[wd] = directory;

            if (!recursive)
                return;

            std::error_code ec;
            for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_directory(ec))
                    AddWatch(Paths::Normalize(it->path().string()));
        }

        void Run()
        {
            alignas(inotify_event) char buffer[64 * 1024];
            std::string movedFrom;
            uint32_t movedCookie = 0;

            while (running)
            {
                pollfd fds[2] = { { inotifyFd, POLLIN, 0 }, { wakePipe[0], POLLIN, 0 } };
                if (poll(fds, 2, -1) < 0)
                    continue;
                if (fds[1].revents & POLLIN)
                    break;
                if (!(fds[0].revents & POLLIN))
                    continue;

                const ssize_t length = read(inotifyFd, buffer, sizeof(buffer));
                if (length <= 0)
                    continue;

                for (const char* cursor = buffer; cursor < buffer + length;)
                {
                    const auto* event = reinterpret_cast<const inotify_event*>(cursor);
                    cursor += sizeof(inotify_event) + event->len;

                    const auto dir = watches.find(event->wd);
                    if (dir == watches.end() || event->len == 0)
                        continue;

                    const std::string full = dir->second + "/" + event->name;
                    const bool isDirectory = (event->mask & IN_ISDIR) != 0;

                    if (event->mask & IN_CREATE)
                    {
                        if (isDirectory && recursive)
                            AddWatch(full);
                        Push({ FileChangeKind::Created, full, {} });
                    }
                    else if (event->mask & (IN_MODIFY | IN_CLOSE_WRITE))
                        Push({ FileChangeKind::Modified, full, {} });
                    else if (event->mask & IN_DELETE)
                        Push({ FileChangeKind::Deleted, full, {} });
                    else if (event->mask & IN_MOVED_FROM)
                    {
                        movedFrom = full;
                        movedCookie = event->cookie;
                        // if the matching MOVED_TO doesn't follow right away, it left the watched tree : a deletion
                        Push({ FileChangeKind::Deleted, full, {} });
                    }
                    else if (event->mask & IN_MOVED_TO)
                    {
                        if (isDirectory && recursive)
                            AddWatch(full);
                        if (event->cookie == movedCookie && !movedFrom.empty())
                        {
                            // replace the deletion pushed for MOVED_FROM by the rename
                            std::lock_guard<std::mutex> lock(mutex);
                            if (!queue.empty() && queue.back().kind == FileChangeKind::Deleted && queue.back().path == movedFrom)
                                queue.pop_back();
                            queue.push_back({ FileChangeKind::Renamed, full, movedFrom });
                            movedFrom.clear();
                        }
                        else
                            Push({ FileChangeKind::Created, full, {} });
                    }
                }
            }
        }
    };

    FileWatcher::FileWatcher() : m_Impl(std::make_unique<Impl>()) {}

    FileWatcher::~FileWatcher()
    {
        Stop();
    }

    bool FileWatcher::Start(const std::string& directory, bool recursive)
    {
        Stop();

        m_Impl->inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (m_Impl->inotifyFd < 0)
            return false;
        if (pipe(m_Impl->wakePipe) != 0)
        {
            close(m_Impl->inotifyFd);
            return false;
        }

        m_Impl->root = Paths::Normalize(directory);
        m_Impl->recursive = recursive;
        m_Impl->watches.clear();
        m_Impl->AddWatch(m_Impl->root);
        if (m_Impl->watches.empty())
        {
            close(m_Impl->inotifyFd);
            close(m_Impl->wakePipe[0]);
            close(m_Impl->wakePipe[1]);
            return false;
        }

        m_Impl->running = true;
        m_Impl->thread = std::thread([impl = m_Impl.get()] { impl->Run(); });
        return true;
    }

    void FileWatcher::Stop()
    {
        if (!m_Impl->running.exchange(false))
            return;

        const char wake = 1;
        ssize_t ignored = write(m_Impl->wakePipe[1], &wake, 1);
        (void)ignored;
        if (m_Impl->thread.joinable())
            m_Impl->thread.join();

        close(m_Impl->inotifyFd);
        close(m_Impl->wakePipe[0]);
        close(m_Impl->wakePipe[1]);
    }

#else   // no inotify (macOS, BSD) : nothing is watched

    struct FileWatcher::Impl {};

    FileWatcher::FileWatcher() : m_Impl(std::make_unique<Impl>()) {}
    FileWatcher::~FileWatcher() = default;
    bool FileWatcher::Start(const std::string&, bool) { return false; }
    void FileWatcher::Stop() {}

#endif

    bool FileWatcher::IsWatching() const
    {
    #if defined(__linux__)
        return m_Impl->running;
    #else
        return false;
    #endif
    }

    std::vector<FileChange> FileWatcher::Poll()
    {
    #if defined(__linux__)
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        std::vector<FileChange> changes;
        changes.swap(m_Impl->queue);
        return changes;
    #else
        return {};
    #endif
    }
}

#endif

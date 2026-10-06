#if defined(_WIN32)

#include "engine/platform/filesystem/file_watcher.hpp"
#include "engine/platform/filesystem/paths.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace Shard::Engine::Core::Platform {

    namespace {
        std::string ToUtf8(const wchar_t* wide, size_t length)
        {
            if (length == 0)
                return {};
            const int size = WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
            std::string utf8(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length), utf8.data(), size, nullptr, nullptr);
            return utf8;
        }

        std::wstring ToWide(const std::string& utf8)
        {
            if (utf8.empty())
                return {};
            const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
            std::wstring wide(static_cast<size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), size);
            return wide;
        }
    }

    struct FileWatcher::Impl {
        HANDLE directory = INVALID_HANDLE_VALUE;
        HANDLE stopEvent = nullptr;
        std::thread thread;
        std::string root;
        bool recursive = true;

        std::mutex mutex;
        std::vector<FileChange> queue;
        std::atomic<bool> running{ false };

        void Push(FileChange change)
        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.push_back(std::move(change));
        }

        alignas(DWORD) char buffer[64 * 1024];
        OVERLAPPED overlapped{};

        // Asks the OS for the next batch of changes. Done by Start() before the thread exists, so that nothing
        // that happens right after Start() returns is missed.
        bool Arm()
        {
            constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                      FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;
            ResetEvent(overlapped.hEvent);
            return ReadDirectoryChangesW(directory, buffer, sizeof(buffer), recursive ? TRUE : FALSE, kFilter, nullptr, &overlapped, nullptr) != 0;
        }

        void Run()
        {
            std::string pendingOld;

            while (running)
            {
                const HANDLE handles[2] = { overlapped.hEvent, stopEvent };
                if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0)
                {
                    CancelIoEx(directory, &overlapped);
                    DWORD ignored = 0;
                    GetOverlappedResult(directory, &overlapped, &ignored, TRUE);
                    break;
                }

                DWORD bytes = 0;
                const bool completed = GetOverlappedResult(directory, &overlapped, &bytes, FALSE) != 0;

                if (completed && bytes > 0)    // 0 bytes : buffer overflow, the changes are lost and there is nothing to report
                {
                    const char* cursor = buffer;
                    for (;;)
                    {
                        const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(cursor);
                        const std::string relative = ToUtf8(info->FileName, info->FileNameLength / sizeof(wchar_t));
                        const std::string full = Paths::Normalize(root + "/" + relative);

                        FileChange change;
                        change.path = full;
                        bool emit = true;
                        switch (info->Action)
                        {
                            case FILE_ACTION_ADDED:             change.kind = FileChangeKind::Created; break;
                            case FILE_ACTION_REMOVED:           change.kind = FileChangeKind::Deleted; break;
                            case FILE_ACTION_MODIFIED:          change.kind = FileChangeKind::Modified; break;
                            case FILE_ACTION_RENAMED_OLD_NAME:  pendingOld = full; emit = false; break;
                            case FILE_ACTION_RENAMED_NEW_NAME:
                                change.kind = FileChangeKind::Renamed;
                                change.oldPath = pendingOld;
                                pendingOld.clear();
                                break;
                            default: emit = false; break;
                        }
                        if (emit)
                            Push(std::move(change));

                        if (info->NextEntryOffset == 0)
                            break;
                        cursor += info->NextEntryOffset;
                    }
                }
                else if (!completed)
                    break;      // the directory went away

                if (!Arm())
                    break;
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

        const HANDLE handle = CreateFileW(ToWide(directory).c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return false;

        m_Impl->directory = handle;
        m_Impl->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_Impl->overlapped = OVERLAPPED{};
        m_Impl->overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_Impl->root = Paths::Normalize(directory);
        m_Impl->recursive = recursive;
        if (!m_Impl->Arm())
        {
            CloseHandle(m_Impl->overlapped.hEvent);
            CloseHandle(m_Impl->stopEvent);
            CloseHandle(handle);
            m_Impl->directory = INVALID_HANDLE_VALUE;
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

        SetEvent(m_Impl->stopEvent);
        if (m_Impl->thread.joinable())
            m_Impl->thread.join();

        CloseHandle(m_Impl->overlapped.hEvent);
        CloseHandle(m_Impl->stopEvent);
        CloseHandle(m_Impl->directory);
        m_Impl->stopEvent = nullptr;
        m_Impl->directory = INVALID_HANDLE_VALUE;
    }

    bool FileWatcher::IsWatching() const
    {
        return m_Impl->running;
    }

    std::vector<FileChange> FileWatcher::Poll()
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        std::vector<FileChange> changes;
        changes.swap(m_Impl->queue);
        return changes;
    }
}

#endif

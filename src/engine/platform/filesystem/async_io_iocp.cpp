#if defined(_WIN32)

#include "engine/platform/filesystem/async_io_backend.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace Shard::Engine::Core::Platform::AsyncIO {

    namespace {
        // One read or write is split in chunks of this size : the next chunk is issued when the previous one completes
        constexpr uint64_t kChunkSize = 1024 * 1024;
        constexpr ULONG_PTR kQuitKey = 1;

        std::wstring ToWide(const std::string& utf8)
        {
            if (utf8.empty())
                return {};
            const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
            std::wstring wide(static_cast<size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), size);
            return wide;
        }

        struct Request {
            OVERLAPPED overlapped{};    // first : the completion hands this address back
            HANDLE file = INVALID_HANDLE_VALUE;
            bool write = false;
            std::string path;
            std::vector<uint8_t> buffer;
            uint64_t offset = 0;
            std::promise<IOResult> promise;
        };

        class IocpBackend : public Backend {
        public:
            explicit IocpBackend(HANDLE port) : m_Port(port)
            {
                m_Thread = std::thread([this] { Run(); });
            }

            ~IocpBackend() override
            {
                Drain();
                PostQueuedCompletionStatus(m_Port, 0, kQuitKey, nullptr);
                m_Thread.join();
                CloseHandle(m_Port);
            }

            const char* Name() const override { return "IOCP"; }

            void Read(std::string path, std::promise<IOResult> promise) override
            {
                auto* request = new Request();
                request->path = std::move(path);
                request->promise = std::move(promise);
                BeginRequest();

                request->file = CreateFileW(ToWide(request->path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                            OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
                if (request->file == INVALID_HANDLE_VALUE)
                    return Fail(request, "can't open " + request->path);

                LARGE_INTEGER size;
                if (!GetFileSizeEx(request->file, &size))
                    return Fail(request, "can't get the size of " + request->path);
                request->buffer.resize(static_cast<size_t>(size.QuadPart));

                Start(request);
            }

            void Write(std::string path, std::vector<uint8_t> data, std::promise<IOResult> promise) override
            {
                auto* request = new Request();
                request->write = true;
                request->path = std::move(path);
                request->buffer = std::move(data);
                request->promise = std::move(promise);
                BeginRequest();

                request->file = CreateFileW(ToWide(request->path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                            FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
                if (request->file == INVALID_HANDLE_VALUE)
                    return Fail(request, "can't open " + request->path + " for writing");

                Start(request);
            }

            void Drain() override
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_Idle.wait(lock, [this] { return m_Pending == 0; });
            }

        private:
            void BeginRequest()
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                ++m_Pending;
            }

            void EndRequest()
            {
                {
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    --m_Pending;
                }
                m_Idle.notify_all();
            }

            // The file is opened : attach it to the port and issue the first chunk (nothing to do for an empty file)
            void Start(Request* request)
            {
                if (request->buffer.empty())
                    return Finish(request);

                if (!CreateIoCompletionPort(request->file, m_Port, 0, 0))
                    return Fail(request, "can't attach " + request->path + " to the completion port");

                Issue(request);
            }

            void Issue(Request* request)
            {
                const uint64_t remaining = request->buffer.size() - request->offset;
                const DWORD length = static_cast<DWORD>(std::min(remaining, kChunkSize));

                request->overlapped.Offset = static_cast<DWORD>(request->offset & 0xFFFFFFFFu);
                request->overlapped.OffsetHigh = static_cast<DWORD>(request->offset >> 32);

                uint8_t* position = request->buffer.data() + request->offset;
                const BOOL started = request->write
                    ? WriteFile(request->file, position, length, nullptr, &request->overlapped)
                    : ReadFile(request->file, position, length, nullptr, &request->overlapped);

                // On success or ERROR_IO_PENDING the completion is queued on the port. Anything else is not.
                if (!started && GetLastError() != ERROR_IO_PENDING)
                    Fail(request, std::string(request->write ? "can't write " : "can't read ") + request->path);
            }

            void Finish(Request* request)
            {
                IOResult result;
                result.ok = true;
                if (!request->write)
                    result.data = std::move(request->buffer);
                Complete(request, std::move(result));
            }

            void Fail(Request* request, std::string error)
            {
                IOResult result;
                result.error = std::move(error);
                Complete(request, std::move(result));
            }

            void Complete(Request* request, IOResult result)
            {
                if (request->file != INVALID_HANDLE_VALUE)
                    CloseHandle(request->file);
                request->promise.set_value(std::move(result));
                delete request;
                EndRequest();
            }

            void Run()
            {
                for (;;)
                {
                    DWORD bytes = 0;
                    ULONG_PTR key = 0;
                    OVERLAPPED* overlapped = nullptr;
                    const BOOL ok = GetQueuedCompletionStatus(m_Port, &bytes, &key, &overlapped, INFINITE);

                    if (!overlapped)
                    {
                        if (key == kQuitKey)
                            return;
                        continue;
                    }

                    auto* request = reinterpret_cast<Request*>(overlapped);
                    if (!ok)
                    {
                        Fail(request, std::string(request->write ? "can't write " : "can't read ") + request->path);
                        continue;
                    }

                    request->offset += bytes;
                    if (request->offset >= request->buffer.size())
                        Finish(request);
                    else if (bytes == 0)
                        Fail(request, "unexpected end of " + request->path);
                    else
                        Issue(request);
                }
            }

            HANDLE m_Port;
            std::thread m_Thread;
            std::mutex m_Mutex;
            std::condition_variable m_Idle;
            int m_Pending = 0;
        };
    }

    std::unique_ptr<Backend> CreateNativeBackend()
    {
        const HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
        if (!port)
            return nullptr;
        return std::make_unique<IocpBackend>(port);
    }
}

#endif

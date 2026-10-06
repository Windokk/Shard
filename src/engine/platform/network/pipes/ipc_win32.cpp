#if defined(_WIN32)

#include "engine/platform/network/pipes/ipc.hpp"
#include "engine/platform/time/clock.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>

namespace Shard::Engine::Core::Platform {

    namespace {
        constexpr DWORD kBufferSize = 64 * 1024;

        std::string PipePath(const std::string& name)
        {
            return "\\\\.\\pipe\\" + name;
        }

        // Completes an overlapped operation in at most timeoutMs. Returns the bytes transferred, or -1 on timeout / error.
        // `error` receives the Win32 error code, ERROR_TIMEOUT on timeout.
        long long Finish(HANDLE handle, OVERLAPPED& overlapped, int timeoutMs, DWORD& error)
        {
            const DWORD wait = WaitForSingleObject(overlapped.hEvent, timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs));
            if (wait == WAIT_TIMEOUT)
            {
                CancelIoEx(handle, &overlapped);
                DWORD ignored = 0;
                GetOverlappedResult(handle, &overlapped, &ignored, TRUE);
                error = ERROR_TIMEOUT;
                return -1;
            }

            DWORD transferred = 0;
            if (!GetOverlappedResult(handle, &overlapped, &transferred, FALSE))
            {
                error = GetLastError();
                return -1;
            }
            error = 0;
            return transferred;
        }

        bool IsBrokenPipe(DWORD error)
        {
            return error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_NO_DATA || error == ERROR_HANDLE_EOF;
        }
    }

    struct NamedPipe::Impl {
        HANDLE handle = INVALID_HANDLE_VALUE;
        HANDLE event = nullptr;     // shared by the operations : the pipe is used by one thread at a time
        bool server = false;
        bool connected = false;
    };

    std::unique_ptr<NamedPipe> NamedPipe::CreateServer(const std::string& name)
    {
        const HANDLE handle = CreateNamedPipeA(PipePath(name).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, kBufferSize, kBufferSize, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return nullptr;

        std::unique_ptr<NamedPipe> pipe(new NamedPipe());
        pipe->m_Impl = std::make_unique<Impl>();
        pipe->m_Impl->handle = handle;
        pipe->m_Impl->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        pipe->m_Impl->server = true;
        return pipe;
    }

    std::unique_ptr<NamedPipe> NamedPipe::Connect(const std::string& name, int timeoutMs)
    {
        const std::string path = PipePath(name);
        const uint64_t deadline = NowNanoseconds() + static_cast<uint64_t>(std::max(timeoutMs, 0)) * 1'000'000ull;

        for (;;)
        {
            const HANDLE handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (handle != INVALID_HANDLE_VALUE)
            {
                std::unique_ptr<NamedPipe> pipe(new NamedPipe());
                pipe->m_Impl = std::make_unique<Impl>();
                pipe->m_Impl->handle = handle;
                pipe->m_Impl->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                pipe->m_Impl->connected = true;
                return pipe;
            }

            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY)
                return nullptr;
            if (NowNanoseconds() >= deadline)
                return nullptr;
            Sleep(10);
        }
    }

    NamedPipe::~NamedPipe()
    {
        if (!m_Impl)
            return;
        if (m_Impl->handle != INVALID_HANDLE_VALUE)
        {
            if (m_Impl->server && m_Impl->connected)
                DisconnectNamedPipe(m_Impl->handle);
            CloseHandle(m_Impl->handle);
        }
        if (m_Impl->event)
            CloseHandle(m_Impl->event);
    }

    bool NamedPipe::WaitForClient(int timeoutMs)
    {
        if (!m_Impl->server)
            return false;
        if (m_Impl->connected)
            return true;

        OVERLAPPED overlapped{};
        overlapped.hEvent = m_Impl->event;
        ResetEvent(m_Impl->event);

        if (ConnectNamedPipe(m_Impl->handle, &overlapped))
        {
            m_Impl->connected = true;
            return true;
        }

        const DWORD error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED)      // the client connected before we asked
        {
            m_Impl->connected = true;
            return true;
        }
        if (error != ERROR_IO_PENDING)
            return false;

        DWORD finishError = 0;
        if (Finish(m_Impl->handle, overlapped, timeoutMs, finishError) < 0)
            return false;
        m_Impl->connected = true;
        return true;
    }

    bool NamedPipe::IsConnected() const
    {
        return m_Impl->connected;
    }

    void NamedPipe::Disconnect()
    {
        if (m_Impl->server && m_Impl->connected)
            DisconnectNamedPipe(m_Impl->handle);
        m_Impl->connected = false;
    }

    IpcStatus NamedPipe::Read(void* buffer, size_t size, size_t* read, int timeoutMs)
    {
        if (read) *read = 0;
        if (!m_Impl->connected)
            return IpcStatus::Closed;

        OVERLAPPED overlapped{};
        overlapped.hEvent = m_Impl->event;
        ResetEvent(m_Impl->event);

        DWORD error = 0;
        if (!ReadFile(m_Impl->handle, buffer, static_cast<DWORD>(size), nullptr, &overlapped))
        {
            error = GetLastError();
            if (error != ERROR_IO_PENDING)
            {
                if (IsBrokenPipe(error)) { m_Impl->connected = false; return IpcStatus::Closed; }
                return IpcStatus::Error;
            }
        }

        const long long transferred = Finish(m_Impl->handle, overlapped, timeoutMs, error);
        if (transferred < 0)
        {
            if (error == ERROR_TIMEOUT) return IpcStatus::TimedOut;
            if (IsBrokenPipe(error)) { m_Impl->connected = false; return IpcStatus::Closed; }
            return IpcStatus::Error;
        }
        if (read) *read = static_cast<size_t>(transferred);
        return IpcStatus::Ok;
    }

    IpcStatus NamedPipe::Write(const void* data, size_t size, int timeoutMs)
    {
        if (!m_Impl->connected)
            return IpcStatus::Closed;

        const char* cursor = static_cast<const char*>(data);
        while (size > 0)
        {
            OVERLAPPED overlapped{};
            overlapped.hEvent = m_Impl->event;
            ResetEvent(m_Impl->event);

            DWORD error = 0;
            if (!WriteFile(m_Impl->handle, cursor, static_cast<DWORD>(size), nullptr, &overlapped))
            {
                error = GetLastError();
                if (error != ERROR_IO_PENDING)
                {
                    if (IsBrokenPipe(error)) { m_Impl->connected = false; return IpcStatus::Closed; }
                    return IpcStatus::Error;
                }
            }

            const long long transferred = Finish(m_Impl->handle, overlapped, timeoutMs, error);
            if (transferred < 0)
            {
                if (error == ERROR_TIMEOUT) return IpcStatus::TimedOut;
                if (IsBrokenPipe(error)) { m_Impl->connected = false; return IpcStatus::Closed; }
                return IpcStatus::Error;
            }
            cursor += transferred;
            size -= static_cast<size_t>(transferred);
        }
        return IpcStatus::Ok;
    }

    // --- Shared memory ---

    namespace {
        // The size is stored in front of the data : the mapping itself is rounded up to the pages
        struct SharedHeader {
            uint64_t size;
            uint64_t reserved;
        };
    }

    struct SharedMemory::Impl {
        HANDLE mapping = nullptr;
        void* view = nullptr;
    };

    std::unique_ptr<SharedMemory> SharedMemory::Create(const std::string& name, size_t size)
    {
        const uint64_t total = static_cast<uint64_t>(size) + sizeof(SharedHeader);
        const HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            static_cast<DWORD>(total >> 32), static_cast<DWORD>(total & 0xFFFFFFFFu), ("Local\\" + name).c_str());
        if (!mapping)
            return nullptr;

        void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!view)
        {
            CloseHandle(mapping);
            return nullptr;
        }
        static_cast<SharedHeader*>(view)->size = size;

        std::unique_ptr<SharedMemory> memory(new SharedMemory());
        memory->m_Impl = std::make_unique<Impl>();
        memory->m_Impl->mapping = mapping;
        memory->m_Impl->view = view;
        return memory;
    }

    std::unique_ptr<SharedMemory> SharedMemory::Open(const std::string& name)
    {
        const HANDLE mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, ("Local\\" + name).c_str());
        if (!mapping)
            return nullptr;

        void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!view)
        {
            CloseHandle(mapping);
            return nullptr;
        }

        std::unique_ptr<SharedMemory> memory(new SharedMemory());
        memory->m_Impl = std::make_unique<Impl>();
        memory->m_Impl->mapping = mapping;
        memory->m_Impl->view = view;
        return memory;
    }

    SharedMemory::~SharedMemory()
    {
        if (!m_Impl)
            return;
        if (m_Impl->view)
            UnmapViewOfFile(m_Impl->view);
        if (m_Impl->mapping)
            CloseHandle(m_Impl->mapping);
    }

    void* SharedMemory::Data() const
    {
        return static_cast<char*>(m_Impl->view) + sizeof(SharedHeader);
    }

    size_t SharedMemory::Size() const
    {
        return static_cast<size_t>(static_cast<SharedHeader*>(m_Impl->view)->size);
    }
}

#endif

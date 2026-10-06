#if !defined(_WIN32)

#include "engine/platform/network/pipes/ipc.hpp"
#include "engine/platform/time/clock.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace Shard::Engine::Core::Platform {

    namespace {
        std::string SocketPath(const std::string& name)
        {
            const char* runtime = std::getenv("XDG_RUNTIME_DIR");
            return std::string(runtime && *runtime ? runtime : "/tmp") + "/" + name + ".sock";
        }

        bool FillAddress(const std::string& path, sockaddr_un& address)
        {
            if (path.size() >= sizeof(address.sun_path))
                return false;
            std::memset(&address, 0, sizeof(address));
            address.sun_family = AF_UNIX;
            std::memcpy(address.sun_path, path.c_str(), path.size());
            return true;
        }

        // 1 : ready, 0 : timeout, -1 : error
        int WaitFd(int fd, short events, int timeoutMs)
        {
            pollfd p{ fd, events, 0 };
            for (;;)
            {
                const int result = poll(&p, 1, timeoutMs);
                if (result < 0 && errno == EINTR)
                    continue;
                return result;
            }
        }
    }

    struct NamedPipe::Impl {
        int listenFd = -1;      // server : the listening socket
        int fd = -1;            // the connection
        bool server = false;
        std::string path;
    };

    std::unique_ptr<NamedPipe> NamedPipe::CreateServer(const std::string& name)
    {
        sockaddr_un address;
        const std::string path = SocketPath(name);
        if (!FillAddress(path, address))
            return nullptr;

        const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0)
            return nullptr;

        unlink(path.c_str());   // a previous crashed server left its file behind
        if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(fd, 1) != 0)
        {
            close(fd);
            return nullptr;
        }

        std::unique_ptr<NamedPipe> pipe(new NamedPipe());
        pipe->m_Impl = std::make_unique<Impl>();
        pipe->m_Impl->listenFd = fd;
        pipe->m_Impl->server = true;
        pipe->m_Impl->path = path;
        return pipe;
    }

    std::unique_ptr<NamedPipe> NamedPipe::Connect(const std::string& name, int timeoutMs)
    {
        sockaddr_un address;
        if (!FillAddress(SocketPath(name), address))
            return nullptr;

        const uint64_t deadline = NowNanoseconds() + static_cast<uint64_t>(std::max(timeoutMs, 0)) * 1'000'000ull;
        for (;;)
        {
            const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (fd < 0)
                return nullptr;

            if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0)
            {
                std::unique_ptr<NamedPipe> pipe(new NamedPipe());
                pipe->m_Impl = std::make_unique<Impl>();
                pipe->m_Impl->fd = fd;
                return pipe;
            }

            const int error = errno;
            close(fd);
            if ((error != ENOENT && error != ECONNREFUSED) || NowNanoseconds() >= deadline)
                return nullptr;
            usleep(10 * 1000);
        }
    }

    NamedPipe::~NamedPipe()
    {
        if (!m_Impl)
            return;
        if (m_Impl->fd >= 0)
            close(m_Impl->fd);
        if (m_Impl->listenFd >= 0)
            close(m_Impl->listenFd);
        if (m_Impl->server && !m_Impl->path.empty())
            unlink(m_Impl->path.c_str());
    }

    bool NamedPipe::WaitForClient(int timeoutMs)
    {
        if (!m_Impl->server)
            return false;
        if (m_Impl->fd >= 0)
            return true;
        if (WaitFd(m_Impl->listenFd, POLLIN, timeoutMs) <= 0)
            return false;

        const int fd = accept4(m_Impl->listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0)
            return false;
        m_Impl->fd = fd;
        return true;
    }

    bool NamedPipe::IsConnected() const
    {
        return m_Impl->fd >= 0;
    }

    void NamedPipe::Disconnect()
    {
        if (m_Impl->fd >= 0)
        {
            close(m_Impl->fd);
            m_Impl->fd = -1;
        }
    }

    IpcStatus NamedPipe::Read(void* buffer, size_t size, size_t* read, int timeoutMs)
    {
        if (read) *read = 0;
        if (m_Impl->fd < 0)
            return IpcStatus::Closed;

        const int ready = WaitFd(m_Impl->fd, POLLIN, timeoutMs);
        if (ready == 0) return IpcStatus::TimedOut;
        if (ready < 0) return IpcStatus::Error;

        const ssize_t result = recv(m_Impl->fd, buffer, size, 0);
        if (result == 0 || (result < 0 && (errno == ECONNRESET || errno == EPIPE)))
        {
            Disconnect();
            return IpcStatus::Closed;
        }
        if (result < 0)
            return IpcStatus::Error;
        if (read) *read = static_cast<size_t>(result);
        return IpcStatus::Ok;
    }

    IpcStatus NamedPipe::Write(const void* data, size_t size, int timeoutMs)
    {
        if (m_Impl->fd < 0)
            return IpcStatus::Closed;

        const char* cursor = static_cast<const char*>(data);
        while (size > 0)
        {
            const int ready = WaitFd(m_Impl->fd, POLLOUT, timeoutMs);
            if (ready == 0) return IpcStatus::TimedOut;
            if (ready < 0) return IpcStatus::Error;

            const ssize_t result = send(m_Impl->fd, cursor, size, MSG_NOSIGNAL);
            if (result < 0)
            {
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                if (errno == EPIPE || errno == ECONNRESET)
                {
                    Disconnect();
                    return IpcStatus::Closed;
                }
                return IpcStatus::Error;
            }
            cursor += result;
            size -= static_cast<size_t>(result);
        }
        return IpcStatus::Ok;
    }

    // --- Shared memory ---

    namespace {
        struct SharedHeader {
            uint64_t size;
            uint64_t reserved;
        };
    }

    struct SharedMemory::Impl {
        void* view = nullptr;
        size_t total = 0;
        std::string name;
        bool owner = false;
    };

    std::unique_ptr<SharedMemory> SharedMemory::Create(const std::string& name, size_t size)
    {
        const std::string shmName = "/" + name;
        const size_t total = size + sizeof(SharedHeader);

        shm_unlink(shmName.c_str());    // a previous crashed owner left its block behind
        const int fd = shm_open(shmName.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0)
            return nullptr;
        if (ftruncate(fd, static_cast<off_t>(total)) != 0)
        {
            close(fd);
            shm_unlink(shmName.c_str());
            return nullptr;
        }

        void* view = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (view == MAP_FAILED)
        {
            shm_unlink(shmName.c_str());
            return nullptr;
        }
        static_cast<SharedHeader*>(view)->size = size;

        std::unique_ptr<SharedMemory> memory(new SharedMemory());
        memory->m_Impl = std::make_unique<Impl>();
        memory->m_Impl->view = view;
        memory->m_Impl->total = total;
        memory->m_Impl->name = shmName;
        memory->m_Impl->owner = true;
        return memory;
    }

    std::unique_ptr<SharedMemory> SharedMemory::Open(const std::string& name)
    {
        const std::string shmName = "/" + name;
        const int fd = shm_open(shmName.c_str(), O_RDWR, 0600);
        if (fd < 0)
            return nullptr;

        struct stat info{};
        if (fstat(fd, &info) != 0 || static_cast<size_t>(info.st_size) < sizeof(SharedHeader))
        {
            close(fd);
            return nullptr;
        }

        const size_t total = static_cast<size_t>(info.st_size);
        void* view = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (view == MAP_FAILED)
            return nullptr;

        std::unique_ptr<SharedMemory> memory(new SharedMemory());
        memory->m_Impl = std::make_unique<Impl>();
        memory->m_Impl->view = view;
        memory->m_Impl->total = total;
        memory->m_Impl->name = shmName;
        return memory;
    }

    SharedMemory::~SharedMemory()
    {
        if (!m_Impl)
            return;
        if (m_Impl->view)
            munmap(m_Impl->view, m_Impl->total);
        if (m_Impl->owner)
            shm_unlink(m_Impl->name.c_str());
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

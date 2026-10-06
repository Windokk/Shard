#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Shard::Engine::Core::Platform {

    enum class IpcStatus { Ok, TimedOut, Closed, Error };

    /// @brief Duplex byte stream between two processes of the same machine (editor <-> game <-> tools).
    /// Windows named pipe / Unix domain socket. One client per server.
    class NamedPipe {
    public:
        /// @brief Creates the endpoint called `name` (a plain identifier : "shard_editor_1234"). nullptr on failure.
        static std::unique_ptr<NamedPipe> CreateServer(const std::string& name);

        /// @brief Connects to the server called `name`, retrying for up to timeoutMs while it doesn't exist yet.
        static std::unique_ptr<NamedPipe> Connect(const std::string& name, int timeoutMs = 0);

        ~NamedPipe();
        NamedPipe(const NamedPipe&) = delete;
        NamedPipe& operator=(const NamedPipe&) = delete;

        /// @brief Server only : waits for a client to connect. False on timeout (timeoutMs < 0 waits forever).
        bool WaitForClient(int timeoutMs = -1);

        bool IsConnected() const;
        /// @brief Drops the current client : a server can WaitForClient again.
        void Disconnect();

        /// @brief Reads up to `size` bytes (at least 1 unless there is a timeout / the peer closed).
        IpcStatus Read(void* buffer, size_t size, size_t* read, int timeoutMs = -1);

        /// @brief Writes all the bytes.
        IpcStatus Write(const void* data, size_t size, int timeoutMs = -1);

    private:
        NamedPipe() = default;
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    /// @brief A block of memory shared by several processes. The creator owns the name : it disappears with it on POSIX.
    class SharedMemory {
    public:
        static std::unique_ptr<SharedMemory> Create(const std::string& name, size_t size);
        static std::unique_ptr<SharedMemory> Open(const std::string& name);

        ~SharedMemory();
        SharedMemory(const SharedMemory&) = delete;
        SharedMemory& operator=(const SharedMemory&) = delete;

        void* Data() const;
        size_t Size() const;

    private:
        SharedMemory() = default;
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}

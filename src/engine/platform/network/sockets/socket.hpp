#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Shard::Engine::Core::Platform {

    enum class SocketType { UDP, TCP };

    enum class SocketStatus {
        Ok,
        WouldBlock,
        TimedOut,
        Closed,
        Error
    };

    struct SocketAddress {
        std::string host;
        uint16_t port = 0;
    };

    class Socket {
    public:
        static std::unique_ptr<Socket> Create(SocketType type);

        ~Socket();
        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        SocketType Type() const { return m_Type; }

        bool Bind(const SocketAddress& address);
        uint16_t LocalPort() const;

        bool Listen(int backlog = 16);
        std::unique_ptr<Socket> Accept(SocketAddress* peer = nullptr);
        bool Connect(const SocketAddress& address, int timeoutMs = -1);

        SocketStatus Send(const void* data, size_t size, size_t* sent = nullptr);
        SocketStatus Receive(void* buffer, size_t size, size_t* received);

        SocketStatus SendTo(const void* data, size_t size, const SocketAddress& to, size_t* sent = nullptr);
        SocketStatus ReceiveFrom(void* buffer, size_t size, size_t* received, SocketAddress* from = nullptr);

        bool SetBlocking(bool blocking);
        bool SetNoDelay(bool enabled);
        bool SetReuseAddress(bool enabled);
        bool SetBroadcast(bool enabled);

        bool WaitReadable(int timeoutMs);

        void Close();
        bool IsOpen() const;

        int LastError() const { return m_LastError; }

    private:
        Socket() = default;

        SocketType m_Type = SocketType::TCP;
        std::intptr_t m_Handle = -1;
        int m_LastError = 0;
    };
}

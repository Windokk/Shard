#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Shard::Engine::Core::Platform {

    enum class SocketType { UDP, TCP };

    enum class SocketStatus {
        Ok,
        WouldBlock,     // non blocking socket with nothing to do right now
        TimedOut,
        Closed,         // TCP : the peer closed the connection
        Error           // see Socket::LastError()
    };

    /// @brief IPv4 endpoint. `host` is a dotted address or a name ("localhost"); empty means any local address.
    struct SocketAddress {
        std::string host;
        uint16_t port = 0;
    };

    /// @brief A UDP or TCP socket (BSD sockets / Winsock). Blocking until SetBlocking(false).
    /// Winsock is started with the first socket and stopped with the last one.
    class Socket {
    public:
        static std::unique_ptr<Socket> Create(SocketType type);

        ~Socket();
        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        SocketType Type() const { return m_Type; }

        bool Bind(const SocketAddress& address);
        /// @brief Port the socket is bound to (useful after binding port 0), 0 if not bound.
        uint16_t LocalPort() const;

        // --- TCP ---
        bool Listen(int backlog = 16);
        /// @brief Takes a pending connection. nullptr when none is pending (non blocking) or on error.
        std::unique_ptr<Socket> Accept(SocketAddress* peer = nullptr);
        /// @brief Connects. timeoutMs < 0 : wait for the system's own timeout.
        bool Connect(const SocketAddress& address, int timeoutMs = -1);

        SocketStatus Send(const void* data, size_t size, size_t* sent = nullptr);
        SocketStatus Receive(void* buffer, size_t size, size_t* received);

        // --- UDP ---
        SocketStatus SendTo(const void* data, size_t size, const SocketAddress& to, size_t* sent = nullptr);
        SocketStatus ReceiveFrom(void* buffer, size_t size, size_t* received, SocketAddress* from = nullptr);

        // --- Options ---
        bool SetBlocking(bool blocking);
        bool SetNoDelay(bool enabled);           // TCP : no Nagle
        bool SetReuseAddress(bool enabled);
        bool SetBroadcast(bool enabled);         // UDP

        /// @brief Waits until data (or a connection to accept) is available. False on timeout. timeoutMs < 0 waits forever.
        bool WaitReadable(int timeoutMs);

        void Close();
        bool IsOpen() const;

        /// @brief Error code of the last failed call (errno / WSAGetLastError()).
        int LastError() const { return m_LastError; }

    private:
        Socket() = default;

        SocketType m_Type = SocketType::TCP;
        std::intptr_t m_Handle = -1;
        int m_LastError = 0;
    };
}

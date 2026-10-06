#include "engine/platform/network/sockets/socket.hpp"

#include <atomic>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

namespace Shard::Engine::Core::Platform {

    namespace {
    #if defined(_WIN32)
        using Handle = SOCKET;
        constexpr Handle kInvalid = INVALID_SOCKET;
        int LastNetworkError() { return WSAGetLastError(); }
        bool IsWouldBlock(int e) { return e == WSAEWOULDBLOCK; }
        bool IsInProgress(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
        bool IsConnectionClosed(int e) { return e == WSAECONNRESET || e == WSAECONNABORTED || e == WSAESHUTDOWN || e == WSAENOTCONN; }
        void CloseHandle_(Handle h) { closesocket(h); }

        // Winsock has to be started once, and stopped when the last socket is gone.
        std::mutex g_WinsockMutex;
        int g_WinsockUsers = 0;
        bool AcquireNetwork()
        {
            std::lock_guard<std::mutex> lock(g_WinsockMutex);
            if (g_WinsockUsers == 0)
            {
                WSADATA data;
                if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
                    return false;
            }
            ++g_WinsockUsers;
            return true;
        }
        void ReleaseNetwork()
        {
            std::lock_guard<std::mutex> lock(g_WinsockMutex);
            if (--g_WinsockUsers == 0)
                WSACleanup();
        }
    #else
        using Handle = int;
        constexpr Handle kInvalid = -1;
        int LastNetworkError() { return errno; }
        bool IsWouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN; }
        bool IsInProgress(int e) { return e == EINPROGRESS || e == EWOULDBLOCK || e == EAGAIN; }
        bool IsConnectionClosed(int e) { return e == ECONNRESET || e == EPIPE || e == ENOTCONN; }
        void CloseHandle_(Handle h) { close(h); }
        bool AcquireNetwork() { return true; }
        void ReleaseNetwork() {}
    #endif

        Handle ToHandle(std::intptr_t h) { return static_cast<Handle>(h); }

        bool ToSockAddr(const SocketAddress& address, sockaddr_in& out)
        {
            std::memset(&out, 0, sizeof(out));
            out.sin_family = AF_INET;
            out.sin_port = htons(address.port);

            if (address.host.empty())
            {
                out.sin_addr.s_addr = htonl(INADDR_ANY);
                return true;
            }
            if (inet_pton(AF_INET, address.host.c_str(), &out.sin_addr) == 1)
                return true;

            addrinfo hints{};
            hints.ai_family = AF_INET;
            addrinfo* result = nullptr;
            if (getaddrinfo(address.host.c_str(), nullptr, &hints, &result) != 0 || !result)
                return false;
            out.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
            freeaddrinfo(result);
            return true;
        }

        SocketAddress FromSockAddr(const sockaddr_in& in)
        {
            char host[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &in.sin_addr, host, sizeof(host));
            return { host, ntohs(in.sin_port) };
        }

        // Waits for the socket to become readable (events = POLLIN) or writable (POLLOUT)
        bool WaitFor(Handle h, short events, int timeoutMs)
        {
        #if defined(_WIN32)
            WSAPOLLFD fd{ h, events, 0 };
            return WSAPoll(&fd, 1, timeoutMs) > 0;
        #else
            pollfd fd{ h, events, 0 };
            return poll(&fd, 1, timeoutMs) > 0;
        #endif
        }

    }

    std::unique_ptr<Socket> Socket::Create(SocketType type)
    {
        if (!AcquireNetwork())
            return nullptr;

        const Handle h = ::socket(AF_INET, type == SocketType::TCP ? SOCK_STREAM : SOCK_DGRAM, type == SocketType::TCP ? IPPROTO_TCP : IPPROTO_UDP);
        if (h == kInvalid)
        {
            ReleaseNetwork();
            return nullptr;
        }

        std::unique_ptr<Socket> socket(new Socket());
        socket->m_Type = type;
        socket->m_Handle = static_cast<std::intptr_t>(h);
        return socket;
    }

    Socket::~Socket()
    {
        Close();
    }

    void Socket::Close()
    {
        if (m_Handle != static_cast<std::intptr_t>(kInvalid))
        {
            CloseHandle_(ToHandle(m_Handle));
            m_Handle = static_cast<std::intptr_t>(kInvalid);
            ReleaseNetwork();
        }
    }

    bool Socket::IsOpen() const
    {
        return m_Handle != static_cast<std::intptr_t>(kInvalid);
    }

    bool Socket::Bind(const SocketAddress& address)
    {
        sockaddr_in addr;
        if (!ToSockAddr(address, addr))
            return false;
        if (::bind(ToHandle(m_Handle), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            m_LastError = LastNetworkError();
            return false;
        }
        return true;
    }

    uint16_t Socket::LocalPort() const
    {
        sockaddr_in addr{};
    #if defined(_WIN32)
        int length = sizeof(addr);
    #else
        socklen_t length = sizeof(addr);
    #endif
        if (getsockname(ToHandle(m_Handle), reinterpret_cast<sockaddr*>(&addr), &length) != 0)
            return 0;
        return ntohs(addr.sin_port);
    }

    bool Socket::Listen(int backlog)
    {
        if (::listen(ToHandle(m_Handle), backlog) != 0)
        {
            m_LastError = LastNetworkError();
            return false;
        }
        return true;
    }

    std::unique_ptr<Socket> Socket::Accept(SocketAddress* peer)
    {
        sockaddr_in addr{};
    #if defined(_WIN32)
        int length = sizeof(addr);
    #else
        socklen_t length = sizeof(addr);
    #endif
        const Handle h = ::accept(ToHandle(m_Handle), reinterpret_cast<sockaddr*>(&addr), &length);
        if (h == kInvalid)
        {
            m_LastError = LastNetworkError();
            return nullptr;
        }

        // The accepted socket is one more user of the network layer
        AcquireNetwork();
        std::unique_ptr<Socket> socket(new Socket());
        socket->m_Type = SocketType::TCP;
        socket->m_Handle = static_cast<std::intptr_t>(h);
        if (peer)
            *peer = FromSockAddr(addr);
        return socket;
    }

    bool Socket::Connect(const SocketAddress& address, int timeoutMs)
    {
        sockaddr_in addr;
        if (!ToSockAddr(address, addr))
            return false;

        if (timeoutMs < 0)
        {
            if (::connect(ToHandle(m_Handle), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
            {
                m_LastError = LastNetworkError();
                return false;
            }
            return true;
        }

        // With a timeout : connect in non blocking mode and wait for the socket to become writable
        SetBlocking(false);
        bool connected = ::connect(ToHandle(m_Handle), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
        if (!connected && IsInProgress(LastNetworkError()))
        {
            if (WaitFor(ToHandle(m_Handle), POLLOUT, timeoutMs))
            {
                int error = 0;
            #if defined(_WIN32)
                int length = sizeof(error);
            #else
                socklen_t length = sizeof(error);
            #endif
                getsockopt(ToHandle(m_Handle), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
                connected = error == 0;
                m_LastError = error;
            }
            else
                m_LastError = 0;
        }
        else if (!connected)
            m_LastError = LastNetworkError();

        SetBlocking(true);
        return connected;
    }

    SocketStatus Socket::Send(const void* data, size_t size, size_t* sent)
    {
        if (sent) *sent = 0;
    #if defined(_WIN32)
        const int result = ::send(ToHandle(m_Handle), static_cast<const char*>(data), static_cast<int>(size), 0);
    #elif defined(MSG_NOSIGNAL)
        const ssize_t result = ::send(ToHandle(m_Handle), data, size, MSG_NOSIGNAL);
    #else
        const ssize_t result = ::send(ToHandle(m_Handle), data, size, 0);
    #endif
        if (result < 0)
        {
            m_LastError = LastNetworkError();
            if (IsWouldBlock(m_LastError)) return SocketStatus::WouldBlock;
            return IsConnectionClosed(m_LastError) ? SocketStatus::Closed : SocketStatus::Error;
        }
        if (sent) *sent = static_cast<size_t>(result);
        return SocketStatus::Ok;
    }

    SocketStatus Socket::Receive(void* buffer, size_t size, size_t* received)
    {
        if (received) *received = 0;
    #if defined(_WIN32)
        const int result = ::recv(ToHandle(m_Handle), static_cast<char*>(buffer), static_cast<int>(size), 0);
    #else
        const ssize_t result = ::recv(ToHandle(m_Handle), buffer, size, 0);
    #endif
        if (result < 0)
        {
            m_LastError = LastNetworkError();
            if (IsWouldBlock(m_LastError)) return SocketStatus::WouldBlock;
            return IsConnectionClosed(m_LastError) ? SocketStatus::Closed : SocketStatus::Error;
        }
        if (result == 0 && m_Type == SocketType::TCP && size > 0)
            return SocketStatus::Closed;    // orderly shutdown by the peer
        if (received) *received = static_cast<size_t>(result);
        return SocketStatus::Ok;
    }

    SocketStatus Socket::SendTo(const void* data, size_t size, const SocketAddress& to, size_t* sent)
    {
        if (sent) *sent = 0;
        sockaddr_in addr;
        if (!ToSockAddr(to, addr))
            return SocketStatus::Error;

    #if defined(_WIN32)
        const int result = ::sendto(ToHandle(m_Handle), static_cast<const char*>(data), static_cast<int>(size), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    #else
        const ssize_t result = ::sendto(ToHandle(m_Handle), data, size, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    #endif
        if (result < 0)
        {
            m_LastError = LastNetworkError();
            return IsWouldBlock(m_LastError) ? SocketStatus::WouldBlock : SocketStatus::Error;
        }
        if (sent) *sent = static_cast<size_t>(result);
        return SocketStatus::Ok;
    }

    SocketStatus Socket::ReceiveFrom(void* buffer, size_t size, size_t* received, SocketAddress* from)
    {
        if (received) *received = 0;
        sockaddr_in addr{};
    #if defined(_WIN32)
        int length = sizeof(addr);
        const int result = ::recvfrom(ToHandle(m_Handle), static_cast<char*>(buffer), static_cast<int>(size), 0, reinterpret_cast<sockaddr*>(&addr), &length);
    #else
        socklen_t length = sizeof(addr);
        const ssize_t result = ::recvfrom(ToHandle(m_Handle), buffer, size, 0, reinterpret_cast<sockaddr*>(&addr), &length);
    #endif
        if (result < 0)
        {
            m_LastError = LastNetworkError();
        #if defined(_WIN32)
            // a previous send to a closed port is reported on the next receive of a UDP socket : not an error of this one
            if (m_LastError == WSAECONNRESET)
                return SocketStatus::WouldBlock;
        #endif
            return IsWouldBlock(m_LastError) ? SocketStatus::WouldBlock : SocketStatus::Error;
        }
        if (received) *received = static_cast<size_t>(result);
        if (from) *from = FromSockAddr(addr);
        return SocketStatus::Ok;
    }

    bool Socket::SetBlocking(bool blocking)
    {
    #if defined(_WIN32)
        u_long mode = blocking ? 0 : 1;
        return ioctlsocket(ToHandle(m_Handle), FIONBIO, &mode) == 0;
    #else
        const int flags = fcntl(ToHandle(m_Handle), F_GETFL, 0);
        if (flags < 0)
            return false;
        return fcntl(ToHandle(m_Handle), F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK)) == 0;
    #endif
    }

    namespace {
        bool SetIntOption(std::intptr_t handle, int level, int option, bool enabled)
        {
            const int value = enabled ? 1 : 0;
            return setsockopt(ToHandle(handle), level, option, reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
        }
    }

    bool Socket::SetNoDelay(bool enabled)       { return SetIntOption(m_Handle, IPPROTO_TCP, TCP_NODELAY, enabled); }
    bool Socket::SetReuseAddress(bool enabled)  { return SetIntOption(m_Handle, SOL_SOCKET, SO_REUSEADDR, enabled); }
    bool Socket::SetBroadcast(bool enabled)     { return SetIntOption(m_Handle, SOL_SOCKET, SO_BROADCAST, enabled); }

    bool Socket::WaitReadable(int timeoutMs)
    {
        return WaitFor(ToHandle(m_Handle), POLLIN, timeoutMs);
    }
}

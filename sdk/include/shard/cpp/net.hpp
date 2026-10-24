// Shard C++ SDK : UDP and TCP sockets, for the networking a game builds itself (shard_net.h).
//
//   // a server : datagrams on a port
//   Shard::Net::UdpSocket server = Shard::Net::OpenUdp(27015, [&](const Shard::Net::Event& e) {
//       if (e.kind == Shard::Net::EventKind::Data) HandleDatagram(e.peer, e.data, e.size);
//   });
//   server.SendTo({ "192.0.2.7", 27016 }, bytes.data(), bytes.size());
//
//   // a client : a stream
//   Shard::Net::TcpSocket client = Shard::Net::Connect({ "example.com", 4000 }, [&](const Shard::Net::Event& e) { ... });
//
// The events arrive on the main thread, at the start of the frame (Net::Poll() reads just before a fixed step). The plugin declares the
// permission "net" in its manifest. The sockets close with the object, and with the plugin.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <shard/shard_net.h>
#include <shard/cpp/detail.hpp>

namespace Shard {
    namespace Net {

        enum class EventKind : uint32_t { Connected = SHARD_NET_EVENT_CONNECTED, Accepted = SHARD_NET_EVENT_ACCEPTED, Data = SHARD_NET_EVENT_DATA, Closed = SHARD_NET_EVENT_CLOSED, Error = SHARD_NET_EVENT_ERROR };

        struct Address {
            std::string host;           ///< a dotted address, a name, or empty (any local address, to bind)
            uint32_t port = 0;
        };

        struct Event {
            EventKind kind = EventKind::Data;
            ShardSocket socket = 0;     ///< Accepted : the new connection
            ShardSocket listener = 0;   ///< Accepted : the listener
            std::string_view peer;      ///< "192.0.2.7:4000" ; valid during the callback
            const void* data = nullptr; ///< Data : valid during the callback
            uint64_t size = 0;
            int32_t error = 0;
        };

        using Handler = std::function<void(const Event&)>;

        namespace detail_net {
            inline const ShardNetApi& Api() { return Shard::detail::Require<ShardNetApi>(SHARD_IFACE_NET); }
            inline void Thunk(void* user, ShardPluginCtx, const ShardNetEvent* e)
            {
                Event event;
                event.kind = EventKind(e->kind); event.socket = e->socket; event.listener = e->listener; event.peer = View(e->peer);
                event.data = e->data; event.size = e->size; event.error = e->error;
                Shard::detail::NoThrow([&] { (*static_cast<Handler*>(user))(event); });
            }
            inline ShardNetAddress Convert(const Address& a)
            {
                ShardNetAddress out{};
                out.structSize = sizeof(ShardNetAddress);
                out.port = a.port;
                out.host = Str(a.host);
                return out;
            }
        }

        /// A socket the plugin owns : closed with the object. A TCP socket accepted by a listener is wrapped with Socket::Adopt.
        class Socket {
        public:
            Socket() = default;
            Socket(Socket&& o) noexcept : m_Socket(o.m_Socket), m_Handler(std::move(o.m_Handler)) { o.m_Socket = 0; }
            Socket& operator=(Socket&& o) noexcept
            {
                if (this != &o) { Close(); m_Socket = o.m_Socket; m_Handler = std::move(o.m_Handler); o.m_Socket = 0; }
                return *this;
            }
            Socket(const Socket&) = delete;
            Socket& operator=(const Socket&) = delete;
            ~Socket() { Close(); }

            explicit operator bool() const { return m_Socket != 0; }
            ShardSocket Handle() const { return m_Socket; }

            /// TCP : queues the bytes. False if the queue is full (nothing was queued) or the socket is closed.
            bool Send(const void* data, uint64_t size) const { return detail_net::Api().send(Shard::detail::PluginHandle(), m_Socket, data, size) == SHARD_OK; }
            bool Send(std::string_view bytes) const { return Send(bytes.data(), bytes.size()); }
            /// UDP : one datagram
            bool SendTo(const Address& to, const void* data, uint64_t size) const
            {
                const ShardNetAddress a = detail_net::Convert(to);
                return detail_net::Api().send_to(Shard::detail::PluginHandle(), m_Socket, &a, data, size) == SHARD_OK;
            }
            uint32_t LocalPort() const { uint32_t port = 0; detail_net::Api().local_port(Shard::detail::PluginHandle(), m_Socket, &port); return port; }
            uint64_t QueuedBytes() const { uint64_t n = 0; detail_net::Api().queued_bytes(Shard::detail::PluginHandle(), m_Socket, &n); return n; }
            bool SetOption(uint32_t option, int32_t value) const { return detail_net::Api().set_option(Shard::detail::PluginHandle(), m_Socket, option, value) == SHARD_OK; }
            void Close()
            {
                if (m_Socket && Shard::detail::Host()) {
                    if (const ShardNetApi* api = Shard::detail::Interface<ShardNetApi>(SHARD_IFACE_NET)) api->close(Shard::detail::PluginHandle(), m_Socket);
                }
                m_Socket = 0;
            }

            /// A connection a listener accepted (Event::socket of an Accepted event) : it reports to the listener's handler, `handler` is not used for it
            static Socket Adopt(ShardSocket socket) { Socket s; s.m_Socket = socket; return s; }

        private:
            friend Socket OpenUdp(uint32_t port, Handler handler, std::string_view host);
            friend Socket Listen(const Address& bind, Handler handler, int backlog);
            friend Socket Connect(const Address& remote, Handler handler);
            ShardSocket m_Socket = 0;
            std::unique_ptr<Handler> m_Handler;
        };
        using UdpSocket = Socket;
        using TcpSocket = Socket;

        /// A UDP socket bound to `port` (0 : any free port, LocalPort() says which). Throws Shard::Error if the address is in use or the plugin is over its limit.
        inline Socket OpenUdp(uint32_t port, Handler handler, std::string_view host = {})
        {
            Socket s;
            s.m_Handler = std::make_unique<Handler>(std::move(handler));
            const Address bind{ std::string(host), port };
            const ShardNetAddress a = detail_net::Convert(bind);
            Shard::detail::Check(detail_net::Api().udp_open(Shard::detail::PluginHandle(), &a, &detail_net::Thunk, s.m_Handler.get(), &s.m_Socket), "a UDP socket");
            return s;
        }
        /// Listens for TCP connections ; each client is an Accepted event
        inline Socket Listen(const Address& bind, Handler handler, int backlog = 16)
        {
            Socket s;
            s.m_Handler = std::make_unique<Handler>(std::move(handler));
            const ShardNetAddress a = detail_net::Convert(bind);
            Shard::detail::Check(detail_net::Api().tcp_listen(Shard::detail::PluginHandle(), &a, backlog, &detail_net::Thunk, s.m_Handler.get(), &s.m_Socket), "a TCP listener");
            return s;
        }
        /// Starts connecting (the name is resolved off the frame) : the outcome is a Connected or an Error event. Bytes sent meanwhile are queued.
        inline Socket Connect(const Address& remote, Handler handler)
        {
            Socket s;
            s.m_Handler = std::make_unique<Handler>(std::move(handler));
            const ShardNetAddress a = detail_net::Convert(remote);
            Shard::detail::Check(detail_net::Api().tcp_connect(Shard::detail::PluginHandle(), &a, &detail_net::Thunk, s.m_Handler.get(), &s.m_Socket), "a TCP connection");
            return s;
        }
        /// Reads the sockets now and delivers what arrived (the host also does it at the start of every frame)
        inline void Poll() { detail_net::Api().poll(Shard::detail::PluginHandle()); }
    }
}

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <shard/shard_net.h>

#include "engine/platform/network/sockets/socket.hpp"
#include "engine/plugins/abi/c_handles.hpp"
#include "engine/plugins/abi/c_internal.hpp"

// shard.net : UDP and TCP sockets, non blocking, driven by the frame. A socket belongs to the plugin that opened it ; what happens to it (data,
// a connection, a close) is reported to that plugin's callback on the main thread, when the host polls (the start of the frame, or poll()).

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        using Core::Platform::Socket;
        using Core::Platform::SocketAddress;
        using Core::Platform::SocketStatus;
        using Core::Platform::SocketType;

        constexpr size_t kMaxSocketsPerPlugin = 64;
        constexpr size_t kMaxQueuedBytes = 1024 * 1024;
        constexpr int kMaxEventsPerSocket = 256;
        constexpr size_t kMaxDatagram = 65507;

        enum class Role { Udp, Listener, Client, Accepted };

        struct NetSocket {
            PluginOwner owner = 0;
            Role role = Role::Udp;
            std::unique_ptr<Socket> socket;
            ShardNetFn fn = nullptr;
            void* user = nullptr;
            ShardSocket listener = 0;           // Accepted : the listener
            std::string peer;
            std::vector<char> sendQueue;        // TCP : bytes not written yet
            size_t sendOffset = 0;
            // tcp_connect : the connection is made by a thread, the poll takes over once it is done
            std::atomic<int> connect{ 0 };      // 0 : not connecting, 1 : in progress, 2 : connected, 3 : failed
            std::atomic<int> connectError{ 0 };
            std::atomic<bool> abandoned{ false };
            bool announced = false;             // the CONNECTED / ERROR event was delivered
            bool closed = false;
        };

        OwnedTable<NetSocket> gSockets;
        std::mutex gNetMutex;
        std::unordered_map<PluginOwner, bool> gHooked;

        std::string Describe(const SocketAddress& a) { return a.host + ":" + std::to_string(a.port); }

        bool ReadAddress(const ShardNetAddress* a, SocketAddress& out, bool needHost)
        {
            if (!a || a->structSize < offsetof(ShardNetAddress, host) + sizeof(ShardStr) || a->port > 65535) return false;
            out.host = ToString(a->host);
            out.port = uint16_t(a->port);
            return !needHost || !out.host.empty();
        }

        void Dispatch(const std::shared_ptr<NetSocket>& s, uint64_t handle, uint32_t kind, uint64_t listener, const std::string& peer, const void* data, uint64_t size, int32_t error)
        {
            if (!s->fn) return;
            PluginContext* ctx = FindContext(ShardPluginCtx(s->owner));
            if (!ctx) return;
            ShardNetEvent e{};
            e.structSize = sizeof(ShardNetEvent);
            e.kind = kind;
            e.socket = handle;
            e.listener = listener;
            e.peer = ToStr(peer);
            e.data = data;
            e.size = size;
            e.error = error;
            ActiveScope scope(ActiveCounter(*ctx), s->owner);
            try { s->fn(s->user, ShardPluginCtx(s->owner), &e); } catch (...) {}
        }

        void CloseSocket(uint64_t handle, const std::shared_ptr<NetSocket>& s)
        {
            if (s->closed) return;
            s->closed = true;
            s->abandoned = true;
            if (s->connect.load() != 1 && s->socket) s->socket->Close();    // a connecting socket is closed by the thread that owns it, when it is done
            gSockets.Remove(s->owner, handle);
        }

        // Writes what the TCP queue holds
        bool Flush(NetSocket& s)
        {
            while (s.sendOffset < s.sendQueue.size()) {
                size_t sent = 0;
                const SocketStatus status = s.socket->Send(s.sendQueue.data() + s.sendOffset, s.sendQueue.size() - s.sendOffset, &sent);
                if (status == SocketStatus::Ok) { s.sendOffset += sent; if (!sent) break; continue; }
                if (status == SocketStatus::WouldBlock) break;
                return false;
            }
            if (s.sendOffset >= s.sendQueue.size()) { s.sendQueue.clear(); s.sendOffset = 0; }
            else if (s.sendOffset > 64 * 1024) { s.sendQueue.erase(s.sendQueue.begin(), s.sendQueue.begin() + long(s.sendOffset)); s.sendOffset = 0; }
            return true;
        }

        void PollOne(uint64_t handle, const std::shared_ptr<NetSocket>& s)
        {
            if (s->closed) return;
            std::vector<char> buffer;

            if (s->role == Role::Client && !s->announced) {
                const int state = s->connect.load();
                if (state == 1) return;
                s->announced = true;
                if (state == 3) {
                    const int error = s->connectError.load();
                    Dispatch(s, handle, SHARD_NET_EVENT_ERROR, 0, s->peer, nullptr, 0, error ? error : -1);
                    CloseSocket(handle, s);
                    return;
                }
                s->socket->SetBlocking(false);
                s->socket->SetNoDelay(true);
                Dispatch(s, handle, SHARD_NET_EVENT_CONNECTED, 0, s->peer, nullptr, 0, 0);
                if (s->closed) return;
            }

            if (s->role == Role::Udp) {
                buffer.resize(kMaxDatagram);
                for (int i = 0; i < kMaxEventsPerSocket && !s->closed; ++i) {
                    size_t received = 0;
                    SocketAddress from;
                    const SocketStatus status = s->socket->ReceiveFrom(buffer.data(), buffer.size(), &received, &from);
                    if (status == SocketStatus::WouldBlock) break;
                    if (status == SocketStatus::Error) { Dispatch(s, handle, SHARD_NET_EVENT_ERROR, 0, "", nullptr, 0, s->socket->LastError()); break; }
                    if (status != SocketStatus::Ok) break;
                    Dispatch(s, handle, SHARD_NET_EVENT_DATA, 0, Describe(from), buffer.data(), received, 0);
                }
                return;
            }

            if (s->role == Role::Listener) {
                for (int i = 0; i < 16 && !s->closed; ++i) {
                    SocketAddress peer;
                    std::unique_ptr<Socket> accepted = s->socket->Accept(&peer);
                    if (!accepted) break;
                    if (gSockets.CountOf(s->owner) >= kMaxSocketsPerPlugin) continue;       // over its limit : the client is dropped
                    accepted->SetBlocking(false);
                    accepted->SetNoDelay(true);
                    auto client = std::make_shared<NetSocket>();
                    client->owner = s->owner;
                    client->role = Role::Accepted;
                    client->socket = std::move(accepted);
                    client->fn = s->fn;
                    client->user = s->user;
                    client->listener = handle;
                    client->peer = Describe(peer);
                    client->announced = true;
                    const uint64_t clientHandle = gSockets.Add(s->owner, client);
                    Dispatch(s, clientHandle, SHARD_NET_EVENT_ACCEPTED, handle, client->peer, nullptr, 0, 0);
                }
                return;
            }

            // A TCP connection
            if (!Flush(*s)) {
                Dispatch(s, handle, SHARD_NET_EVENT_ERROR, 0, s->peer, nullptr, 0, s->socket->LastError());
                CloseSocket(handle, s);
                return;
            }
            buffer.resize(64 * 1024);
            for (int i = 0; i < kMaxEventsPerSocket && !s->closed; ++i) {
                size_t received = 0;
                const SocketStatus status = s->socket->Receive(buffer.data(), buffer.size(), &received);
                if (status == SocketStatus::Ok && received > 0) { Dispatch(s, handle, SHARD_NET_EVENT_DATA, 0, s->peer, buffer.data(), received, 0); continue; }
                if (status == SocketStatus::WouldBlock || status == SocketStatus::TimedOut) break;
                if (status == SocketStatus::Closed || (status == SocketStatus::Ok && received == 0)) {
                    Dispatch(s, handle, SHARD_NET_EVENT_CLOSED, 0, s->peer, nullptr, 0, 0);
                    CloseSocket(handle, s);
                    break;
                }
                Dispatch(s, handle, SHARD_NET_EVENT_ERROR, 0, s->peer, nullptr, 0, s->socket->LastError());
                CloseSocket(handle, s);
                break;
            }
        }

        void HookRelease(PluginContext& ctx, PluginOwner owner)
        {
            {
                std::lock_guard<std::mutex> lock(gNetMutex);
                if (gHooked[owner]) return;
                gHooked[owner] = true;
            }
            ctx.OnRelease("net", [owner] {
                for (auto& s : gSockets.RemoveOwner(owner)) {
                    s->closed = true;
                    s->abandoned = true;
                    if (s->connect.load() != 1 && s->socket) s->socket->Close();
                }
                std::lock_guard<std::mutex> lock(gNetMutex);
                gHooked.erase(owner);
            });
        }

        PluginContext* Prepare(ShardPluginCtx handle, ShardResult& result)
        {
            PluginContext* ctx = FindContext(handle);
            if (!ctx) { result = SHARD_E_STALE_HANDLE; return nullptr; }
            if (!OnMainThread()) { result = SHARD_E_WRONG_THREAD; return nullptr; }
            if (gSockets.CountOf(PluginOwner(handle)) >= kMaxSocketsPerPlugin) { result = SHARD_E_OUT_OF_MEMORY; return nullptr; }
            result = SHARD_OK;
            return ctx;
        }

        ShardResult Open(ShardPluginCtx handle, Role role, const ShardNetAddress* bind, int32_t backlog, ShardNetFn fn, void* user, ShardSocket* out)
        {
            if (!out || !fn) return SHARD_E_INVALID_ARG;
            *out = 0;
            ShardResult prepared;
            PluginContext* ctx = Prepare(handle, prepared);
            if (!ctx) return prepared;
            SocketAddress address;
            if (bind && !ReadAddress(bind, address, false)) return SHARD_E_INVALID_ARG;
            std::unique_ptr<Socket> socket = Socket::Create(role == Role::Udp ? SocketType::UDP : SocketType::TCP);
            if (!socket) return SHARD_E_INTERNAL;
            if (role == Role::Listener) socket->SetReuseAddress(true);
            if (!socket->Bind(address)) return SHARD_E_ALREADY_EXISTS;
            if (role == Role::Listener && !socket->Listen(std::max(1, backlog))) return SHARD_E_INTERNAL;
            socket->SetBlocking(false);
            auto s = std::make_shared<NetSocket>();
            s->owner = PluginOwner(handle);
            s->role = role;
            s->socket = std::move(socket);
            s->fn = fn;
            s->user = user;
            s->announced = true;
            HookRelease(*ctx, PluginOwner(handle));
            *out = gSockets.Add(PluginOwner(handle), s);
            return SHARD_OK;
        }

        ShardResult SHARD_CALL UdpOpen(ShardPluginCtx ctx, const ShardNetAddress* bind, ShardNetFn fn, void* user, ShardSocket* out)
        {
            try { return Open(ctx, Role::Udp, bind, 0, fn, user, out); } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL TcpListen(ShardPluginCtx ctx, const ShardNetAddress* bind, int32_t backlog, ShardNetFn fn, void* user, ShardSocket* out)
        {
            try { return Open(ctx, Role::Listener, bind, backlog, fn, user, out); } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL TcpConnect(ShardPluginCtx handle, const ShardNetAddress* remote, ShardNetFn fn, void* user, ShardSocket* out)
        {
            try {
                if (!out || !fn) return SHARD_E_INVALID_ARG;
                *out = 0;
                ShardResult prepared;
                PluginContext* ctx = Prepare(handle, prepared);
                if (!ctx) return prepared;
                SocketAddress address;
                if (!ReadAddress(remote, address, true)) return SHARD_E_INVALID_ARG;
                std::unique_ptr<Socket> socket = Socket::Create(SocketType::TCP);
                if (!socket) return SHARD_E_INTERNAL;
                auto s = std::make_shared<NetSocket>();
                s->owner = PluginOwner(handle);
                s->role = Role::Client;
                s->socket = std::move(socket);
                s->fn = fn;
                s->user = user;
                s->peer = Describe(address);
                s->connect = 1;
                HookRelease(*ctx, PluginOwner(handle));
                *out = gSockets.Add(PluginOwner(handle), s);
                // The connection (the name's resolution included) is made off the frame
                std::thread([s, address] {
                    const bool ok = s->socket->Connect(address, 10000);
                    s->connectError = ok ? 0 : s->socket->LastError();
                    if (!ok || s->abandoned.load()) s->socket->Close();
                    s->connect = ok && !s->abandoned.load() ? 2 : 3;
                }).detach();
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Send(ShardPluginCtx handle, ShardSocket socket, const void* data, uint64_t size)
        {
            try {
                if (!data && size) return SHARD_E_INVALID_ARG;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                std::shared_ptr<NetSocket> s = gSockets.Get(PluginOwner(handle), socket);
                if (!s || s->closed) return SHARD_E_STALE_HANDLE;
                if (s->role != Role::Client && s->role != Role::Accepted) return SHARD_E_INVALID_ARG;
                if (s->sendQueue.size() - s->sendOffset + size > kMaxQueuedBytes) return SHARD_E_OUT_OF_MEMORY;
                s->sendQueue.insert(s->sendQueue.end(), static_cast<const char*>(data), static_cast<const char*>(data) + size);
                if (s->connect.load() != 1 && !Flush(*s)) return SHARD_OK;      // the error is reported by the next poll
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SendTo(ShardPluginCtx handle, ShardSocket socket, const ShardNetAddress* remote, const void* data, uint64_t size)
        {
            try {
                if (!data && size) return SHARD_E_INVALID_ARG;
                if (size > kMaxDatagram) return SHARD_E_INVALID_ARG;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                std::shared_ptr<NetSocket> s = gSockets.Get(PluginOwner(handle), socket);
                if (!s || s->closed) return SHARD_E_STALE_HANDLE;
                if (s->role != Role::Udp) return SHARD_E_INVALID_ARG;
                SocketAddress to;
                if (!ReadAddress(remote, to, true)) return SHARD_E_INVALID_ARG;
                size_t sent = 0;
                s->socket->SendTo(data, size_t(size), to, &sent);       // a datagram the system cannot take is dropped
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Close(ShardPluginCtx handle, ShardSocket socket)
        {
            try {
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                std::shared_ptr<NetSocket> s = gSockets.Get(PluginOwner(handle), socket);
                if (!s || s->closed) return SHARD_E_STALE_HANDLE;
                if (s->role == Role::Accepted || s->role == Role::Client) Flush(*s);       // what was sent goes out before the close
                CloseSocket(socket, s);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL LocalPort(ShardPluginCtx handle, ShardSocket socket, uint32_t* port)
        {
            try {
                if (!port) return SHARD_E_INVALID_ARG;
                std::shared_ptr<NetSocket> s = gSockets.Get(PluginOwner(handle), socket);
                if (!s || s->closed) return SHARD_E_STALE_HANDLE;
                *port = s->socket ? s->socket->LocalPort() : 0;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL QueuedBytes(ShardPluginCtx handle, ShardSocket socket, uint64_t* bytes)
        {
            try {
                if (!bytes) return SHARD_E_INVALID_ARG;
                std::shared_ptr<NetSocket> s = gSockets.Get(PluginOwner(handle), socket);
                if (!s || s->closed) return SHARD_E_STALE_HANDLE;
                *bytes = s->sendQueue.size() - s->sendOffset;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetOption(ShardPluginCtx handle, ShardSocket socket, uint32_t option, int32_t value)
        {
            try {
                std::shared_ptr<NetSocket> s = gSockets.Get(PluginOwner(handle), socket);
                if (!s || s->closed || !s->socket) return SHARD_E_STALE_HANDLE;
                switch (option) {
                    case SHARD_NET_OPT_NODELAY: return s->socket->SetNoDelay(value != 0) ? SHARD_OK : SHARD_E_UNSUPPORTED;
                    case SHARD_NET_OPT_BROADCAST: return s->socket->SetBroadcast(value != 0) ? SHARD_OK : SHARD_E_UNSUPPORTED;
                    case SHARD_NET_OPT_REUSE_ADDR: return s->socket->SetReuseAddress(value != 0) ? SHARD_OK : SHARD_E_UNSUPPORTED;
                    case SHARD_NET_OPT_RECV_BUFFER:
                    case SHARD_NET_OPT_SEND_BUFFER: return SHARD_E_UNSUPPORTED;
                    default: return SHARD_E_INVALID_ARG;
                }
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // The handles of an owner's sockets, in the order they were opened (a replay of the traffic delivers in the same order)
        std::vector<uint64_t> HandlesOf(PluginOwner owner)
        {
            std::vector<uint64_t> handles;
            gSockets.ForEach(owner, [&](uint64_t handle, const NetSocket&) { handles.push_back(handle); });
            std::sort(handles.begin(), handles.end(), [](uint64_t a, uint64_t b) { return (a & 0xFFFFFFFFull) < (b & 0xFFFFFFFFull); });
            return handles;
        }

        void PollOwner(PluginOwner owner)
        {
            for (uint64_t handle : HandlesOf(owner)) {
                std::shared_ptr<NetSocket> s = gSockets.Get(owner, handle);
                if (s) PollOne(handle, s);
            }
        }

        ShardResult SHARD_CALL PollApi(ShardPluginCtx handle)
        {
            try {
                if (!FindContext(handle)) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                PollOwner(PluginOwner(handle));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    void NetPollAll()
    {
        std::vector<PluginOwner> owners;
        {
            std::lock_guard<std::mutex> lock(gNetMutex);
            for (const auto& [owner, hooked] : gHooked) owners.push_back(owner);
        }
        std::sort(owners.begin(), owners.end());
        for (PluginOwner owner : owners) PollOwner(owner);
    }

    const void* NetApi()
    {
        static const ShardNetApi api = {
            sizeof(ShardNetApi), &UdpOpen, &TcpListen, &TcpConnect, &Send, &SendTo, &Close, &LocalPort, &QueuedBytes, &SetOption, &PollApi };
        return &api;
    }
}

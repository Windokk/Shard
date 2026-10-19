#include "engine/plugins/handshake.hpp"

#include <algorithm>
#include <filesystem>
#include <thread>

#include "engine/plugins/archive.hpp"
#include "engine/plugins/crypto.hpp"

namespace Shard::Engine::Plugins {

    namespace fs = std::filesystem;
    using nlohmann::json;
    using Core::Platform::Socket;
    using Core::Platform::SocketAddress;
    using Core::Platform::SocketStatus;
    using Core::Platform::SocketType;

    namespace {
        constexpr size_t kMaxJsonFrame = 1024 * 1024;
        constexpr size_t kMaxFileFrame = 256u * 1024 * 1024;

        std::vector<uint8_t> MakeFrame(uint8_t kind, const void* data, size_t size)
        {
            std::vector<uint8_t> frame;
            const uint32_t length = uint32_t(size);
            frame.push_back(uint8_t(length >> 24)); frame.push_back(uint8_t(length >> 16));
            frame.push_back(uint8_t(length >> 8)); frame.push_back(uint8_t(length));
            frame.push_back(kind);
            frame.insert(frame.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
            return frame;
        }

        bool SendAll(Socket& socket, const uint8_t* data, size_t size, int timeoutMs)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (size > 0) {
                size_t sent = 0;
                const SocketStatus st = socket.Send(data, size, &sent);
                if (st == SocketStatus::Ok) { data += sent; size -= sent; continue; }
                if (st == SocketStatus::WouldBlock && std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                return false;
            }
            return true;
        }

        json SetToJson(const std::vector<PluginSetEntry>& set) { return PluginSetToJson(set); }
    }

    // ---- Server ----

    Core::Result<bool> PluginHandshakeServer::Start(const HandshakeServerOptions& options)
    {
        if (m_Listener) return Core::Err("already running");
        m_Options = options;
        auto listener = Socket::Create(SocketType::TCP);
        if (!listener) return Core::Err("no socket available");
        listener->SetReuseAddress(true);
        if (!listener->Bind(SocketAddress{ options.bindAddress, options.port })) return Core::Err("cannot bind port " + std::to_string(options.port));
        if (!listener->Listen()) return Core::Err("cannot listen on port " + std::to_string(options.port));
        listener->SetBlocking(false);
        m_Listener = std::move(listener);
        return Core::Ok(true);
    }

    void PluginHandshakeServer::Stop()
    {
        m_Clients.clear();
        if (m_Listener) m_Listener->Close();
        m_Listener.reset();
    }

    uint16_t PluginHandshakeServer::LocalPort() const { return m_Listener ? m_Listener->LocalPort() : 0; }

    size_t PluginHandshakeServer::ConnectedClients() const
    {
        size_t n = 0;
        for (const auto& c : m_Clients) if (c->welcomed && !c->dropped) ++n;
        return n;
    }

    void PluginHandshakeServer::Send(Client& client, const json& message)
    {
        const std::string text = message.dump();
        const std::vector<uint8_t> frame = MakeFrame(0, text.data(), text.size());
        if (!SendAll(*client.socket, frame.data(), frame.size(), m_Options.clientTimeoutMs)) client.dropped = true;
    }

    void PluginHandshakeServer::SendFile(Client& client, const std::vector<uint8_t>& bytes)
    {
        const std::vector<uint8_t> frame = MakeFrame(1, bytes.data(), bytes.size());
        if (!SendAll(*client.socket, frame.data(), frame.size(), 30000)) client.dropped = true;
    }

    Core::Result<std::vector<uint8_t>> PluginHandshakeServer::PackDatapack(const std::string& id)
    {
        const PluginRecord* record = m_Manager.Find(id);
        if (!record || record->manifest.moduleKind != ModuleKind::Datapack || record->state == PluginState::Failed || record->manifest.rootDir.empty())
            return Core::Err("'" + id + "' is not a datapack this server offers");
        ZipWriter zip;
        auto added = zip.AddDirectory(record->manifest.rootDir, "", { kExtractedMarkerFile, kInstallMarkerFile });
        if (!added) return Core::Err(added.GetError().message);
        return Core::Ok(zip.Finish());
    }

    std::vector<json> PluginHandshakeServer::Offers(const std::vector<PluginSetEntry>& clientSet)
    {
        std::vector<json> offers;
        if (!m_Options.offerDatapacks) return offers;
        for (const PluginSetEntry& mine : m_Manager.SimulationPluginSet()) {
            if (std::any_of(clientSet.begin(), clientSet.end(), [&](const PluginSetEntry& c) { return c.id == mine.id && c.version == mine.version && c.hash == mine.hash; })) continue;
            const PluginRecord* record = m_Manager.Find(mine.id);
            if (!record || record->manifest.moduleKind != ModuleKind::Datapack) continue;          // never code
            auto bytes = PackDatapack(mine.id);
            if (!bytes) continue;
            offers.push_back({ { "id", mine.id }, { "version", mine.version }, { "size", bytes.Value().size() },
                               { "sha256", Crypto::ToHex(Crypto::Sha256Of(bytes.Value().data(), bytes.Value().size())) } });
        }
        return offers;
    }

    void PluginHandshakeServer::Handle(Client& client, const json& message)
    {
        const std::string type = message.is_object() ? message.value("type", std::string()) : std::string();
        if (type == "hello") {
            if (client.welcomed) { client.dropped = true; return; }
            client.info.id = ++m_NextClientId;
            client.info.name = message.value("name", std::string("player"));
            client.info.address = client.address;
            if (client.info.name.size() > 64) client.info.name.resize(64);

            json reply;
            reply["plugins"] = SetToJson(m_Manager.SimulationPluginSet());
            const std::vector<PluginSetEntry> theirs = PluginSetFromJson(message.value("plugins", json::array()));

            ConnectDecision decision;
            if (message.value("protocol", 0u) != kHandshakeProtocol)
                decision.Refuse("protocol " + std::to_string(message.value("protocol", 0u)) + " is not supported (the server speaks " + std::to_string(kHandshakeProtocol) + ")");
            if (decision.allowed) m_Manager.CheckClientPlugins(message.value("plugins", json::array()), decision);
            if (decision.allowed) m_Manager.NotifyClientConnect(client.info, decision);

            if (decision.allowed) {
                reply["type"] = "welcome";
                client.welcomed = true;
                ++m_Accepted;
            } else {
                reply["type"] = "refused";
                reply["reason"] = decision.reason;
                ++m_Refused;
            }
            reply["downloads"] = Offers(theirs);
            Send(client, reply);
            if (!decision.allowed && reply["downloads"].empty()) client.dropped = true;
        } else if (type == "fetch") {
            const std::string id = message.value("id", std::string());
            if (!m_Options.offerDatapacks) { Send(client, { { "type", "error" }, { "reason", "this server does not offer downloads" } }); return; }
            auto bytes = PackDatapack(id);
            if (!bytes) { Send(client, { { "type", "error" }, { "reason", bytes.GetError().message } }); return; }
            SendFile(client, bytes.Value());
        } else {
            client.dropped = true;
        }
    }

    void PluginHandshakeServer::Poll()
    {
        if (!m_Listener) return;
        const auto now = std::chrono::steady_clock::now();

        for (int accepted = 0; accepted < 16; ++accepted) {
            SocketAddress peer;
            auto socket = m_Listener->Accept(&peer);
            if (!socket) break;
            if (m_Clients.size() >= m_Options.maxClients) continue;          // closed by going out of scope
            socket->SetBlocking(false);
            auto client = std::make_unique<Client>();
            client->socket = std::move(socket);
            client->address = peer.host + ":" + std::to_string(peer.port);
            client->lastActivity = now;
            m_Clients.push_back(std::move(client));
        }

        for (auto& holder : m_Clients) {
            Client& client = *holder;
            if (client.dropped) continue;
            uint8_t chunk[4096];
            for (;;) {
                size_t got = 0;
                const SocketStatus st = client.socket->Receive(chunk, sizeof(chunk), &got);
                if (st == SocketStatus::Ok && got > 0) {
                    client.buffer.insert(client.buffer.end(), chunk, chunk + got);
                    client.lastActivity = now;
                    if (client.buffer.size() > kMaxJsonFrame + 8) { client.dropped = true; break; }
                    continue;
                }
                if (st == SocketStatus::Closed || st == SocketStatus::Error) client.dropped = true;
                break;
            }
            // Whole frames
            while (!client.dropped && client.buffer.size() >= 5) {
                const uint32_t length = (uint32_t(client.buffer[0]) << 24) | (uint32_t(client.buffer[1]) << 16) | (uint32_t(client.buffer[2]) << 8) | client.buffer[3];
                const uint8_t kind = client.buffer[4];
                if (kind != 0 || length > kMaxJsonFrame) { client.dropped = true; break; }
                if (client.buffer.size() < 5 + size_t(length)) break;
                json message = json::parse(client.buffer.begin() + 5, client.buffer.begin() + 5 + long(length), nullptr, false);
                client.buffer.erase(client.buffer.begin(), client.buffer.begin() + 5 + long(length));
                if (message.is_discarded()) { client.dropped = true; break; }
                Handle(client, message);
            }
            if (!client.dropped && std::chrono::duration_cast<std::chrono::milliseconds>(now - client.lastActivity).count() > m_Options.clientTimeoutMs && !client.welcomed)
                client.dropped = true;
        }

        for (auto it = m_Clients.begin(); it != m_Clients.end();) {
            if ((*it)->dropped) {
                if ((*it)->welcomed) m_Manager.NotifyClientDisconnect((*it)->info);
                it = m_Clients.erase(it);
            } else ++it;
        }
    }

    // ---- Client ----

    void PluginHandshakeClient::Close()
    {
        if (m_Socket) m_Socket->Close();
        m_Socket.reset();
    }

    bool PluginHandshakeClient::SendJson(const json& message)
    {
        if (!m_Socket) return false;
        const std::string text = message.dump();
        const std::vector<uint8_t> frame = MakeFrame(0, text.data(), text.size());
        return SendAll(*m_Socket, frame.data(), frame.size(), m_TimeoutMs);
    }

    Core::Result<std::vector<uint8_t>> PluginHandshakeClient::ReadFrame(uint8_t& kind, size_t maxBytes)
    {
        if (!m_Socket) return Core::Err("not connected");
        std::vector<uint8_t> buffer;
        size_t need = 5;
        bool haveHeader = false;
        size_t length = 0;
        while (buffer.size() < need) {
            if (!m_Socket->WaitReadable(m_TimeoutMs)) return Core::Err("the server did not answer in time");
            uint8_t chunk[16384];
            size_t got = 0;
            const SocketStatus st = m_Socket->Receive(chunk, std::min(sizeof(chunk), need - buffer.size()), &got);
            if (st == SocketStatus::Closed) return Core::Err("the server closed the connection");
            if (st != SocketStatus::Ok && st != SocketStatus::WouldBlock) return Core::Err("the connection failed");
            buffer.insert(buffer.end(), chunk, chunk + got);
            if (!haveHeader && buffer.size() >= 5) {
                length = (size_t(buffer[0]) << 24) | (size_t(buffer[1]) << 16) | (size_t(buffer[2]) << 8) | buffer[3];
                kind = buffer[4];
                if (length > maxBytes) return Core::Err("the server's answer is larger than allowed");
                haveHeader = true;
                need = 5 + length;
            }
        }
        return Core::Ok(std::vector<uint8_t>(buffer.begin() + 5, buffer.end()));
    }

    Core::Result<json> PluginHandshakeClient::ReadJson()
    {
        uint8_t kind = 0;
        auto frame = ReadFrame(kind, kMaxJsonFrame);
        if (!frame) return Core::Err(frame.GetError().message);
        if (kind != 0) return Core::Err("expected a message and got a file");
        json message = json::parse(frame.Value().begin(), frame.Value().end(), nullptr, false);
        if (message.is_discarded() || !message.is_object()) return Core::Err("the server's answer is not understood");
        return Core::Ok(std::move(message));
    }

    HandshakeResult PluginHandshakeClient::Connect(const std::string& host, uint16_t port, const std::string& playerName, const std::vector<PluginSetEntry>& plugins)
    {
        HandshakeResult result;
        Close();
        m_Socket = Socket::Create(SocketType::TCP);
        if (!m_Socket) { result.reason = "no socket available"; return result; }
        if (!m_Socket->Connect(SocketAddress{ host, port }, m_TimeoutMs)) { result.reason = "cannot connect to " + host + ":" + std::to_string(port); m_Socket.reset(); return result; }
        m_Socket->SetBlocking(false);
        result.connected = true;

        if (!SendJson({ { "type", "hello" }, { "protocol", kHandshakeProtocol }, { "name", playerName }, { "plugins", SetToJson(plugins) } })) {
            result.reason = "cannot talk to the server";
            return result;
        }
        auto reply = ReadJson();
        if (!reply) { result.reason = reply.GetError().message; return result; }
        const json& r = reply.Value();
        const std::string type = r.value("type", std::string());
        result.serverPlugins = PluginSetFromJson(r.value("plugins", json::array()));
        if (r.contains("downloads") && r["downloads"].is_array()) {
            for (const json& d : r["downloads"]) {
                if (!d.is_object()) continue;
                result.downloads.push_back({ d.value("id", std::string()), d.value("version", std::string()), d.value("size", uint64_t(0)), d.value("sha256", std::string()) });
            }
        }
        if (type == "welcome") result.accepted = true;
        else if (type == "refused") result.reason = r.value("reason", std::string("refused"));
        else result.reason = "the server's answer is not understood";
        return result;
    }

    Core::Result<std::string> PluginHandshakeClient::FetchDatapack(const std::string& id, const std::string& expectedSha256, const std::string& directory)
    {
        if (!IsValidPluginId(id)) return Core::Err("'" + id + "' is not a plugin id");
        if (!SendJson({ { "type", "fetch" }, { "id", id } })) return Core::Err("cannot talk to the server");
        uint8_t kind = 0;
        auto frame = ReadFrame(kind, kMaxFileFrame);
        if (!frame) return Core::Err(frame.GetError().message);
        if (kind == 0) {
            json error = json::parse(frame.Value().begin(), frame.Value().end(), nullptr, false);
            return Core::Err(error.is_object() ? error.value("reason", std::string("refused")) : std::string("refused"));
        }
        const auto& bytes = frame.Value();
        const std::string hash = Crypto::ToHex(Crypto::Sha256Of(bytes.data(), bytes.size()));
        if (hash != expectedSha256) return Core::Err("the datapack's SHA-256 is " + hash + ", the server said " + expectedSha256);
        auto zip = ZipArchive::OpenMemory(bytes);
        if (!zip) return Core::Err(zip.GetError().message);

        // A datapack only : data, no binaries
        auto manifestBytes = zip.Value().Read("shard-plugin.json");
        if (!manifestBytes) return Core::Err("the archive has no shard-plugin.json");
        auto manifest = ParseManifestText(std::string(manifestBytes.Value().begin(), manifestBytes.Value().end()));
        if (!manifest) return Core::Err("its manifest is not valid : " + manifest.GetError().message);
        if (manifest.Value().id != id || manifest.Value().moduleKind != ModuleKind::Datapack)
            return Core::Err("the server sent something that is not the datapack '" + id + "'");

        const fs::path target = fs::path(directory) / id;
        std::error_code ec;
        fs::remove_all(target, ec);
        auto extracted = zip.Value().ExtractAll(target.string());
        if (!extracted) return Core::Err(extracted.GetError().message);
        return Core::Ok(target.generic_string());
    }
}

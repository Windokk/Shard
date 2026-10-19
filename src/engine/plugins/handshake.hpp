#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/core/containers/result.hpp"
#include "engine/platform/network/sockets/socket.hpp"
#include "engine/plugins/manager.hpp"

namespace Shard::Engine::Plugins {

    /// @brief The conversation a client and a server have before they play : "these are my plugins" / "these are mine, you may come in, or not".
    ///
    /// TCP, frames of `u32 length (big endian) | u8 kind | payload`. Kind 0 is JSON (at most 1 MB), kind 1 is a file (a datapack archive).
    ///   client > { "type": "hello", "protocol": 1, "name": "...", "plugins": [ {id, version, hash} ] }
    ///   server > { "type": "welcome", "plugins": [...], "downloads": [ {id, version, size, sha256} ] }      the sets match
    ///          | { "type": "refused", "reason": "...", "plugins": [...], "downloads": [...] }              they do not, or a plugin said no
    ///   client > { "type": "fetch", "id": "..." }   server > a kind 1 frame (the .shardplugin of a datapack the server offers) or { "type": "error", ... }
    /// Only datapacks (no code) are ever offered for download. A refused client that is missing some can fetch them, restart, and come back.
    inline constexpr uint32_t kHandshakeProtocol = 1;

    struct HandshakeServerOptions {
        uint16_t port = 0;                          ///< 0 : any free port (LocalPort() says which)
        std::string bindAddress = "";               ///< empty : every interface
        bool offerDatapacks = false;                ///< let clients that lack a datapack download it
        int clientTimeoutMs = 5000;                 ///< a client that says nothing for that long is dropped
        size_t maxClients = 64;
    };

    /// @brief The server's half. Single threaded : call Poll() from the server loop (ShardHeadless --server does, every tick).
    class PluginHandshakeServer {
    public:
        explicit PluginHandshakeServer(PluginManager& manager) : m_Manager(manager) {}
        ~PluginHandshakeServer() { Stop(); }

        Core::Result<bool> Start(const HandshakeServerOptions& options);
        void Stop();
        bool IsRunning() const { return m_Listener != nullptr; }
        uint16_t LocalPort() const;

        /// Accepts, reads and answers whatever is ready. Never blocks.
        void Poll();

        size_t ConnectedClients() const;
        size_t AcceptedCount() const { return m_Accepted; }
        size_t RefusedCount() const { return m_Refused; }

    private:
        struct Client {
            std::unique_ptr<Core::Platform::Socket> socket;
            std::string address;
            std::vector<uint8_t> buffer;
            std::chrono::steady_clock::time_point lastActivity;
            bool welcomed = false;
            ClientInfo info;
            bool dropped = false;
        };

        void Handle(Client& client, const nlohmann::json& message);
        void Send(Client& client, const nlohmann::json& message);
        void SendFile(Client& client, const std::vector<uint8_t>& bytes);
        std::vector<nlohmann::json> Offers(const std::vector<PluginSetEntry>& clientSet);
        Core::Result<std::vector<uint8_t>> PackDatapack(const std::string& id);

        PluginManager& m_Manager;
        HandshakeServerOptions m_Options;
        std::unique_ptr<Core::Platform::Socket> m_Listener;
        std::vector<std::unique_ptr<Client>> m_Clients;
        uint32_t m_NextClientId = 0;
        size_t m_Accepted = 0;
        size_t m_Refused = 0;
    };

    struct HandshakeResult {
        bool connected = false;                     ///< the server answered
        bool accepted = false;                      ///< and let us in
        std::string reason;                         ///< why not
        std::vector<PluginSetEntry> serverPlugins;
        struct Download { std::string id; std::string version; uint64_t size = 0; std::string sha256; };
        std::vector<Download> downloads;            ///< datapacks the server offers and we lack
    };

    /// @brief The client's half. Blocking, with a timeout : a connecting client is not in the middle of a frame.
    class PluginHandshakeClient {
    public:
        explicit PluginHandshakeClient(int timeoutMs = 5000) : m_TimeoutMs(timeoutMs) {}
        ~PluginHandshakeClient() { Close(); }

        /// Connects and says hello with `plugins` (the simulation plugin set of this client)
        HandshakeResult Connect(const std::string& host, uint16_t port, const std::string& playerName, const std::vector<PluginSetEntry>& plugins);

        /// Asks for a datapack the server offered, checks it against `expectedSha256`, and unpacks it into `directory` (a folder named after it is made)
        Core::Result<std::string> FetchDatapack(const std::string& id, const std::string& expectedSha256, const std::string& directory);

        void Close();

    private:
        Core::Result<nlohmann::json> ReadJson();
        Core::Result<std::vector<uint8_t>> ReadFrame(uint8_t& kind, size_t maxBytes);
        bool SendJson(const nlohmann::json& message);

        int m_TimeoutMs;
        std::unique_ptr<Core::Platform::Socket> m_Socket;
    };
}

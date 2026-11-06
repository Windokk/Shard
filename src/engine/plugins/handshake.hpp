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

    // TCP frames: u32 length (big endian) | u8 kind | payload. Kind 0 is JSON (max 1 MB), kind 1 a datapack archive.
    // hello > welcome | refused, fetch > file | error. Only datapacks (no code) are offered for download.
    inline constexpr uint32_t kHandshakeProtocol = 1;

    struct HandshakeServerOptions {
        uint16_t port = 0;
        std::string bindAddress = "";
        bool offerDatapacks = false;
        int clientTimeoutMs = 5000;
        size_t maxClients = 64;
    };

    class PluginHandshakeServer {
    public:
        explicit PluginHandshakeServer(PluginManager& manager) : m_Manager(manager) {}
        ~PluginHandshakeServer() { Stop(); }

        Core::Result<bool> Start(const HandshakeServerOptions& options);
        void Stop();
        bool IsRunning() const { return m_Listener != nullptr; }
        uint16_t LocalPort() const;

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
        bool connected = false;
        bool accepted = false;
        std::string reason;
        std::vector<PluginSetEntry> serverPlugins;
        struct Download { std::string id; std::string version; uint64_t size = 0; std::string sha256; };
        std::vector<Download> downloads;
    };

    class PluginHandshakeClient {
    public:
        explicit PluginHandshakeClient(int timeoutMs = 5000) : m_TimeoutMs(timeoutMs) {}
        ~PluginHandshakeClient() { Close(); }

        HandshakeResult Connect(const std::string& host, uint16_t port, const std::string& playerName, const std::vector<PluginSetEntry>& plugins);

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

#include <gtest/gtest.h>

#include <future>
#include <thread>

#include "engine/plugins/handshake.hpp"
#include "plugins/plugin_test_utils.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using nlohmann::json;
using PluginTest::TempDir;

namespace {
    std::vector<std::string>& TestEvents() { static std::vector<std::string> e; return e; }

    class Doorman final : public ServerPlugin {
    public:
        void OnClientConnect(ClientInfo& client, ConnectDecision& decision) override
        {
            TestEvents().push_back("connect " + client.name);
            if (client.name == "banned") decision.Refuse("you are banned");
        }
        void OnClientDisconnect(const ClientInfo& client) override { TestEvents().push_back("disconnect " + client.name); }
    };
    IPlugin* MakeDoorman() { return new Doorman(); }
    class Plain final : public IModule {};
    bool Logged(const std::string& line) { return std::find(TestEvents().begin(), TestEvents().end(), line) != TestEvents().end(); }
    IPlugin* MakePlain() { return new Plain(); }

    std::string CodeModule(const std::string& id, const std::string& version)
    {
        json j = PluginTest::Manifest(id, version);
        j["kind"] = "module";
        j["moduleKind"] = "code";
        return j.dump();
    }

    class HandshakeTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            PluginManager::Get().ResetForTests();
            TestEvents().clear();
            config.host = Host::Headless;
            config.registerConsoleCommands = false;
            config.readOnly = true;
        }
        void TearDown() override { PluginManager::Get().ResetForTests(); }
        PluginManager& pm() { return PluginManager::Get(); }

        // Answers the server's side while `client` runs on another thread
        template <typename F>
        auto WithServer(PluginHandshakeServer& server, F&& client)
        {
            auto future = std::async(std::launch::async, std::forward<F>(client));
            while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) server.Poll();
            auto result = future.get();
            for (int i = 0; i < 5; ++i) server.Poll();
            return result;
        }

        HandshakeResult Hello(PluginHandshakeServer& server, const std::string& name, const std::vector<PluginSetEntry>& plugins)
        {
            return WithServer(server, [&] {
                PluginHandshakeClient client(3000);
                return client.Connect("127.0.0.1", server.LocalPort(), name, plugins);
            });
        }

        PluginManagerConfig config;
    };
}

TEST_F(HandshakeTest, AClientWithTheServersPluginsIsWelcomed)
{
    pm().AddStaticPlugin(CodeModule("game.logic", "1.2.0"), &MakePlain);
    pm().AddStaticPlugin(PluginTest::Manifest("doorman").dump(), &MakeDoorman);
    ASSERT_TRUE(pm().Startup(config));
    pm().OnEngineSystemsReady(nullptr, nullptr, nullptr);
    pm().OnEngineInitialized();

    PluginHandshakeServer server(pm());
    ASSERT_TRUE(server.Start({}));
    ASSERT_NE(server.LocalPort(), 0);

    auto result = Hello(server, "alice", pm().SimulationPluginSet());
    EXPECT_TRUE(result.connected);
    EXPECT_TRUE(result.accepted) << result.reason;
    EXPECT_EQ(server.AcceptedCount(), 1u);
    ASSERT_EQ(result.serverPlugins.size(), 1u);
    EXPECT_EQ(result.serverPlugins[0].id, "game.logic");
    EXPECT_EQ(std::count(TestEvents().begin(), TestEvents().end(), "connect alice"), 1);
}

TEST_F(HandshakeTest, AClientWithOtherPluginsIsRefusedAndToldWhy)
{
    pm().AddStaticPlugin(CodeModule("game.logic", "1.2.0"), &MakePlain);
    ASSERT_TRUE(pm().Startup(config));
    pm().OnEngineSystemsReady(nullptr, nullptr, nullptr);
    pm().OnEngineInitialized();
    PluginHandshakeServer server(pm());
    ASSERT_TRUE(server.Start({}));

    // missing, other version, extra
    auto none = Hello(server, "bob", {});
    EXPECT_TRUE(none.connected);
    EXPECT_FALSE(none.accepted);
    EXPECT_NE(none.reason.find("game.logic"), std::string::npos) << none.reason;

    auto version = Hello(server, "bob", { { "game.logic", "9.9.9", 0 } });
    EXPECT_FALSE(version.accepted);
    EXPECT_NE(version.reason.find("9.9.9"), std::string::npos) << version.reason;

    auto mine = pm().SimulationPluginSet();
    mine.push_back({ "mod.extra", "1.0.0", 1 });
    EXPECT_FALSE(Hello(server, "bob", mine).accepted);
    EXPECT_EQ(server.RefusedCount(), 3u);
    EXPECT_EQ(server.AcceptedCount(), 0u);
    EXPECT_FALSE(Logged("connect bob"));          // the plugins were wrong : the server plugins were not even asked
}

TEST_F(HandshakeTest, AServerPluginOrAHookCanRefuseAMatchingClient)
{
    pm().AddStaticPlugin(PluginTest::Manifest("doorman").dump(), &MakeDoorman);
    ASSERT_TRUE(pm().Startup(config));
    pm().OnEngineSystemsReady(nullptr, nullptr, nullptr);
    pm().OnEngineInitialized();
    PluginHandshakeServer server(pm());
    ASSERT_TRUE(server.Start({}));
    auto banned = Hello(server, "banned", {});
    EXPECT_FALSE(banned.accepted);
    EXPECT_EQ(banned.reason, "you are banned");
    EXPECT_TRUE(Hello(server, "fine", {}).accepted);
}

TEST_F(HandshakeTest, DisconnectsAreReportedToTheServerPlugins)
{
    pm().AddStaticPlugin(PluginTest::Manifest("doorman").dump(), &MakeDoorman);
    ASSERT_TRUE(pm().Startup(config));
    pm().OnEngineSystemsReady(nullptr, nullptr, nullptr);
    pm().OnEngineInitialized();
    PluginHandshakeServer server(pm());
    ASSERT_TRUE(server.Start({}));
    // A client that stays connected after it was welcomed, then leaves
    PluginHandshakeClient client(3000);
    HandshakeResult result = WithServer(server, [&] { return client.Connect("127.0.0.1", server.LocalPort(), "carol", {}); });
    ASSERT_TRUE(result.accepted);
    EXPECT_EQ(server.ConnectedClients(), 1u);
    client.Close();
    for (int i = 0; i < 50 && server.ConnectedClients() > 0; ++i) { server.Poll(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    EXPECT_EQ(server.ConnectedClients(), 0u);
    EXPECT_EQ(std::count(TestEvents().begin(), TestEvents().end(), "disconnect carol"), 1);
}

TEST_F(HandshakeTest, NothingButWellFormedHellosGetThrough)
{
    ASSERT_TRUE(pm().Startup(config));
    PluginHandshakeServer server(pm());
    HandshakeServerOptions options;
    options.clientTimeoutMs = 150;
    ASSERT_TRUE(server.Start(options));

    auto connect = [&] {
        auto socket = Core::Platform::Socket::Create(Core::Platform::SocketType::TCP);
        EXPECT_TRUE(socket->Connect(Core::Platform::SocketAddress{ "127.0.0.1", server.LocalPort() }, 2000));
        return socket;
    };
    auto closedByServer = [&](Core::Platform::Socket& socket) {
        for (int i = 0; i < 200; ++i) {
            server.Poll();
            if (socket.WaitReadable(1)) {
                uint8_t b[16];
                size_t got = 0;
                const auto st = socket.Receive(b, sizeof(b), &got);
                if (st == Core::Platform::SocketStatus::Closed || st == Core::Platform::SocketStatus::Error) return true;
            }
        }
        return false;
    };

    {   // garbage
        auto s = connect();
        const char junk[] = "GET / HTTP/1.0\r\n\r\n";
        size_t sent = 0;
        s->Send(junk, sizeof(junk), &sent);
        EXPECT_TRUE(closedByServer(*s));
    }
    {   // a frame that claims to be huge
        auto s = connect();
        const uint8_t frame[] = { 0x7f, 0xff, 0xff, 0xff, 0x00 };
        size_t sent = 0;
        s->Send(frame, sizeof(frame), &sent);
        EXPECT_TRUE(closedByServer(*s));
    }
    {   // a frame that is not JSON
        auto s = connect();
        const uint8_t frame[] = { 0, 0, 0, 3, 0, 'a', 'b', 'c' };
        size_t sent = 0;
        s->Send(frame, sizeof(frame), &sent);
        EXPECT_TRUE(closedByServer(*s));
    }
    {   // someone who says nothing
        auto s = connect();
        EXPECT_TRUE(closedByServer(*s));
    }
    EXPECT_EQ(server.AcceptedCount(), 0u);
    // and the server still works
    EXPECT_TRUE(Hello(server, "ok", pm().SimulationPluginSet()).accepted);
}

TEST_F(HandshakeTest, AClientThatSpeaksAnotherProtocolIsRefused)
{
    ASSERT_TRUE(pm().Startup(config));
    PluginHandshakeServer server(pm());
    ASSERT_TRUE(server.Start({}));
    auto socket = Core::Platform::Socket::Create(Core::Platform::SocketType::TCP);
    ASSERT_TRUE(socket->Connect(Core::Platform::SocketAddress{ "127.0.0.1", server.LocalPort() }, 2000));
    const std::string hello = json{ { "type", "hello" }, { "protocol", 99 }, { "name", "future" }, { "plugins", json::array() } }.dump();
    std::vector<uint8_t> frame = { uint8_t(hello.size() >> 24), uint8_t(hello.size() >> 16), uint8_t(hello.size() >> 8), uint8_t(hello.size()), 0 };
    frame.insert(frame.end(), hello.begin(), hello.end());
    size_t sent = 0;
    socket->Send(frame.data(), frame.size(), &sent);
    std::string reply;
    for (int i = 0; i < 200 && reply.size() < 8; ++i) {
        server.Poll();
        if (socket->WaitReadable(1)) { char b[512]; size_t got = 0; if (socket->Receive(b, sizeof(b), &got) == Core::Platform::SocketStatus::Ok) reply.append(b, got); }
    }
    EXPECT_NE(reply.find("refused"), std::string::npos) << reply;
    EXPECT_NE(reply.find("protocol"), std::string::npos) << reply;
}

TEST_F(HandshakeTest, ADatapackTheClientLacksCanBeDownloaded)
{
    TempDir dir;
    dir.Write("server/plugins/pack/shard-plugin.json", PluginTest::DatapackManifest("pack.maps", "2.0.0"));
    dir.Write("server/plugins/pack/content/tables/maps.json", R"({"arena":{"size":10}})");
    config.searchPaths.push_back(dir.Sub("server/plugins"));
    pm().AddStaticPlugin(CodeModule("game.logic", "1.0.0"), &MakePlain);
    ASSERT_TRUE(pm().Startup(config));
    pm().OnEngineSystemsReady(nullptr, nullptr, nullptr);
    pm().OnEngineInitialized();

    PluginHandshakeServer server(pm());
    HandshakeServerOptions options;
    options.offerDatapacks = true;
    ASSERT_TRUE(server.Start(options));

    // the client has the code module but not the datapack
    std::vector<PluginSetEntry> theirs;
    for (const PluginSetEntry& e : pm().SimulationPluginSet()) if (e.id == "game.logic") theirs.push_back(e);
    HandshakeResult result;
    Core::Result<std::string> fetched = Core::Err("not tried");
    PluginHandshakeClient client(5000);
    WithServer(server, [&] {
        result = client.Connect("127.0.0.1", server.LocalPort(), "dave", theirs);
        if (result.downloads.size() == 1)
            fetched = client.FetchDatapack(result.downloads[0].id, result.downloads[0].sha256, dir.Sub("client/downloaded"));
        return 0;
    });
    EXPECT_FALSE(result.accepted);
    ASSERT_EQ(result.downloads.size(), 1u);
    EXPECT_EQ(result.downloads[0].id, "pack.maps");
    EXPECT_EQ(result.downloads[0].version, "2.0.0");
    ASSERT_TRUE(fetched) << fetched.GetError().message;
    EXPECT_EQ(dir.Read("client/downloaded/pack.maps/content/tables/maps.json"), R"({"arena":{"size":10}})");
    EXPECT_FALSE(dir.Read("client/downloaded/pack.maps/shard-plugin.json").empty());
}

TEST_F(HandshakeTest, CodeIsNeverOfferedAndWrongHashesAreRefused)
{
    TempDir dir;
    dir.Write("server/plugins/pack/shard-plugin.json", PluginTest::DatapackManifest("pack.maps", "1.0.0"));
    dir.Write("server/plugins/pack/content/tables/t.json", "{}");
    config.searchPaths.push_back(dir.Sub("server/plugins"));
    pm().AddStaticPlugin(CodeModule("game.logic", "1.0.0"), &MakePlain);
    ASSERT_TRUE(pm().Startup(config));
    pm().OnEngineSystemsReady(nullptr, nullptr, nullptr);
    pm().OnEngineInitialized();
    PluginHandshakeServer server(pm());
    HandshakeServerOptions options;
    options.offerDatapacks = true;
    ASSERT_TRUE(server.Start(options));

    Core::Result<std::string> codeFetch = Core::Err("not tried"), badHash = Core::Err("not tried"), notAPlugin = Core::Err("not tried");
    HandshakeResult result;
    WithServer(server, [&] {
        PluginHandshakeClient client(5000);
        result = client.Connect("127.0.0.1", server.LocalPort(), "erin", {});
        codeFetch = client.FetchDatapack("game.logic", "00", dir.Sub("dl"));
        badHash = client.FetchDatapack("pack.maps", std::string(64, '0'), dir.Sub("dl"));
        notAPlugin = client.FetchDatapack("../etc", "00", dir.Sub("dl"));
        return 0;
    });
    for (const auto& d : result.downloads) EXPECT_NE(d.id, "game.logic");          // only datapacks are on offer
    EXPECT_FALSE(codeFetch);
    EXPECT_NE(codeFetch.GetError().message.find("not a datapack"), std::string::npos) << codeFetch.GetError().message;
    EXPECT_FALSE(badHash);
    EXPECT_NE(badHash.GetError().message.find("SHA-256"), std::string::npos) << badHash.GetError().message;
    EXPECT_FALSE(notAPlugin);
    EXPECT_FALSE(std::filesystem::exists(dir.Sub("dl/pack.maps")));
}

TEST_F(HandshakeTest, WithoutOffersNothingIsServed)
{
    TempDir dir;
    dir.Write("server/plugins/pack/shard-plugin.json", PluginTest::DatapackManifest("pack.maps", "1.0.0"));
    dir.Write("server/plugins/pack/content/tables/t.json", "{}");
    config.searchPaths.push_back(dir.Sub("server/plugins"));
    ASSERT_TRUE(pm().Startup(config));
    PluginHandshakeServer server(pm());
    ASSERT_TRUE(server.Start({}));
    HandshakeResult result;
    Core::Result<std::string> fetched = Core::Err("not tried");
    WithServer(server, [&] {
        PluginHandshakeClient client(5000);
        result = client.Connect("127.0.0.1", server.LocalPort(), "frank", {});
        fetched = client.FetchDatapack("pack.maps", std::string(64, '0'), dir.Sub("dl"));
        return 0;
    });
    EXPECT_FALSE(result.accepted);
    EXPECT_TRUE(result.downloads.empty());
    EXPECT_FALSE(fetched);
}

TEST_F(HandshakeTest, ConnectingToNothingFailsCleanly)
{
    PluginHandshakeClient client(500);
    const HandshakeResult result = client.Connect("127.0.0.1", 1, "nobody", {});
    EXPECT_FALSE(result.connected);
    EXPECT_FALSE(result.accepted);
    EXPECT_FALSE(result.reason.empty());
}

#include <gtest/gtest.h>

#include <atomic>
#include <map>
#include <thread>

#include "engine/platform/network/sockets/socket.hpp"
#include "engine/plugins/archive.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/plugins/package.hpp"
#include "engine/plugins/registry.hpp"
#include "plugins/plugin_test_utils.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using nlohmann::json;
using PluginTest::TempDir;
using Core::Platform::Socket;
using Core::Platform::SocketAddress;
using Core::Platform::SocketType;

namespace {
    struct Publisher {
        Crypto::PublicKey pk;
        Crypto::SecretKey sk;
        explicit Publisher(uint8_t seedByte)
        {
            std::array<uint8_t, 32> seed{};
            seed.fill(seedByte);
            Crypto::KeyPairFromSeed(seed, pk, sk);
        }
    };

    // A datapack folder with a table
    void MakePack(const TempDir& dir, const std::string& folder, const std::string& id, const std::string& version, const std::string& table = R"({"a":1})",
                  json extra = json::object())
    {
        dir.Write(folder + "/shard-plugin.json", PluginTest::DatapackManifest(id, version, extra));
        dir.Write(folder + "/content/tables/t.json", table);
    }

    // Packs, signs (optionally) and indexes a plugin folder into a registry folder ; returns the sha256 of the archive
    std::string Publish(const TempDir& dir, const std::string& folder, const std::string& registryFolder, const std::string& id, const std::string& version,
                        const Publisher* signer, json& index, json dependencies = json::array())
    {
        if (signer) EXPECT_TRUE(SignPackage(dir.Sub(folder), "acme", signer->sk));
        ZipWriter zip;
        EXPECT_TRUE(zip.AddDirectory(dir.Sub(folder)));
        const std::string file = id + "-" + version + std::string(kArchiveExtension);
        const std::vector<uint8_t> bytes = zip.Finish();
        dir.Write(registryFolder + "/" + file, std::string(bytes.begin(), bytes.end()));
        const std::string hash = Crypto::ToHex(Crypto::Sha256Of(bytes.data(), bytes.size()));
        json* entry = nullptr;
        for (json& p : index["plugins"]) if (p["id"] == id) entry = &p;
        if (!entry) {
            index["plugins"].push_back({ { "id", id }, { "name", "Plugin " + id }, { "description", "a test plugin" }, { "versions", json::array() } });
            entry = &index["plugins"].back();
        }
        (*entry)["versions"].push_back({ { "version", version }, { "file", file }, { "sha256", hash }, { "size", bytes.size() }, { "publisher", "acme" }, { "dependencies", dependencies } });
        return hash;
    }

    json NewIndex() { return json{ { "schema", 1 }, { "name", "Test registry" }, { "plugins", json::array() } }; }

    // A very small HTTP/1.0 server
    class TestHttpServer {
    public:
        struct Route { int status = 200; std::vector<uint8_t> body; std::string location; bool truncate = false; };

        TestHttpServer()
        {
            m_Listener = Socket::Create(SocketType::TCP);
            m_Listener->SetReuseAddress(true);
            m_Listener->Bind(SocketAddress{ "127.0.0.1", 0 });
            m_Listener->Listen();
            m_Port = m_Listener->LocalPort();
            m_Thread = std::thread([this] { Serve(); });
        }
        ~TestHttpServer()
        {
            m_Stop = true;
            m_Thread.join();
        }
        uint16_t Port() const { return m_Port; }
        std::string Url(const std::string& path = "") const { return "http://127.0.0.1:" + std::to_string(m_Port) + path; }
        void Set(const std::string& path, Route route) { std::lock_guard<std::mutex> l(m_Mutex); m_Routes[path] = std::move(route); }
        void SetBytes(const std::string& path, const std::string& text) { Route r; r.body.assign(text.begin(), text.end()); Set(path, r); }

    private:
        void Serve()
        {
            while (!m_Stop) {
                if (!m_Listener->WaitReadable(50)) continue;
                auto client = m_Listener->Accept();
                if (!client) continue;
                std::string request;
                char buffer[2048];
                while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
                    if (!client->WaitReadable(2000)) break;
                    size_t got = 0;
                    if (client->Receive(buffer, sizeof(buffer), &got) != Core::Platform::SocketStatus::Ok) break;
                    request.append(buffer, got);
                }
                std::string path = "/";
                const size_t sp1 = request.find(' ');
                const size_t sp2 = request.find(' ', sp1 + 1);
                if (sp1 != std::string::npos && sp2 != std::string::npos) path = request.substr(sp1 + 1, sp2 - sp1 - 1);

                Route route;
                bool found = false;
                { std::lock_guard<std::mutex> l(m_Mutex); auto it = m_Routes.find(path); if (it != m_Routes.end()) { route = it->second; found = true; } }
                std::string head;
                if (!found) { head = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n"; route.body.clear(); }
                else if (route.status >= 300 && route.status < 400) { head = "HTTP/1.0 " + std::to_string(route.status) + " Moved\r\nLocation: " + route.location + "\r\nContent-Length: 0\r\n\r\n"; route.body.clear(); }
                else head = "HTTP/1.0 200 OK\r\nContent-Length: " + std::to_string(route.body.size() + (route.truncate ? 100 : 0)) + "\r\n\r\n";
                Send(*client, head.data(), head.size());
                if (!route.body.empty()) Send(*client, route.body.data(), route.body.size());
            }
        }
        static void Send(Socket& s, const void* data, size_t size)
        {
            const char* p = static_cast<const char*>(data);
            while (size > 0) {
                size_t sent = 0;
                if (s.Send(p, size, &sent) != Core::Platform::SocketStatus::Ok) return;
                p += sent; size -= sent;
            }
        }

        std::unique_ptr<Socket> m_Listener;
        uint16_t m_Port = 0;
        std::thread m_Thread;
        std::atomic<bool> m_Stop{ false };
        std::mutex m_Mutex;
        std::map<std::string, Route> m_Routes;
    };

    std::vector<uint8_t> FileBytes(const TempDir& dir, const std::string& rel)
    {
        const std::string s = dir.Read(rel);
        return std::vector<uint8_t>(s.begin(), s.end());
    }
}

// ---- Signatures ----

TEST(PluginPackage, ASignedFolderVerifiesAndAnyChangeBreaksIt)
{
    TempDir dir;
    Publisher acme(1);
    MakePack(dir, "p", "pack.signed", "1.0.0");
    TrustStore trust;
    EXPECT_EQ(VerifyPackage(dir.Sub("p"), trust).status, SignatureInfo::Status::Unsigned);

    ASSERT_TRUE(SignPackage(dir.Sub("p"), "acme", acme.sk));
    EXPECT_EQ(VerifyPackage(dir.Sub("p"), trust).status, SignatureInfo::Status::Untrusted);
    trust.Trust("acme", acme.pk);
    SignatureInfo ok = VerifyPackage(dir.Sub("p"), trust);
    EXPECT_EQ(ok.status, SignatureInfo::Status::Valid) << ok.message;
    EXPECT_EQ(ok.publisher, "acme");

    // a changed file
    dir.Write("p/content/tables/t.json", R"({"a":2})");
    EXPECT_EQ(VerifyPackage(dir.Sub("p"), trust).status, SignatureInfo::Status::Invalid);
    dir.Write("p/content/tables/t.json", R"({"a":1})");
    EXPECT_EQ(VerifyPackage(dir.Sub("p"), trust).status, SignatureInfo::Status::Valid);

    // an added file
    dir.Write("p/content/extra.txt", "x");
    EXPECT_EQ(VerifyPackage(dir.Sub("p"), trust).status, SignatureInfo::Status::Invalid);
    std::filesystem::remove(dir.Sub("p/content/extra.txt"));

    // the signature belongs to an id and a version : the same files under another version are not signed
    dir.Write("p/shard-plugin.json", PluginTest::DatapackManifest("pack.signed", "1.0.1"));
    EXPECT_EQ(VerifyPackage(dir.Sub("p"), trust).status, SignatureInfo::Status::Invalid);
}

TEST(PluginPackage, AForgedOrSwappedSignatureIsInvalid)
{
    TempDir dir;
    Publisher acme(1), mallory(2);
    MakePack(dir, "p", "pack.forged", "1.0.0");
    ASSERT_TRUE(SignPackage(dir.Sub("p"), "acme", acme.sk));
    TrustStore trust;
    trust.Trust("acme", acme.pk);

    // Mallory signs the same files and claims to be acme
    MakePack(dir, "q", "pack.forged", "1.0.0");
    ASSERT_TRUE(SignPackage(dir.Sub("q"), "acme", mallory.sk));
    SignatureInfo forged = VerifyPackage(dir.Sub("q"), trust);
    EXPECT_EQ(forged.status, SignatureInfo::Status::Untrusted) << "the signature is real, but the key is not the trusted acme's";

    // Acme's key written over Mallory's signature
    json sig = json::parse(dir.Read("q/shard-signature.json"));
    sig["publicKey"] = Crypto::ToHex(acme.pk);
    dir.Write("q/shard-signature.json", sig.dump());
    EXPECT_EQ(VerifyPackage(dir.Sub("q"), trust).status, SignatureInfo::Status::Invalid);

    dir.Write("q/shard-signature.json", "not json");
    EXPECT_EQ(VerifyPackage(dir.Sub("q"), trust).status, SignatureInfo::Status::Invalid);
}

TEST(PluginPackage, ARevokedKeyIsRefusedEvenWhenTrusted)
{
    TempDir dir;
    Publisher acme(1);
    MakePack(dir, "p", "pack.revoked", "1.0.0");
    ASSERT_TRUE(SignPackage(dir.Sub("p"), "acme", acme.sk));
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    EXPECT_EQ(VerifyPackage(dir.Sub("p"), trust).status, SignatureInfo::Status::Valid);
    trust.Revoke(acme.pk);
    EXPECT_EQ(VerifyPackage(dir.Sub("p"), trust).status, SignatureInfo::Status::Revoked);
    EXPECT_FALSE(trust.IsTrusted("acme", acme.pk));
}

TEST(PluginPackage, TheDigestDoesNotDependOnTheFileSystemOrTheMarkers)
{
    TempDir a, b;
    for (const char* name : { "z.txt", "a.txt", "sub/m.txt" }) {
        a.Write(std::string("p/") + name, name);
    }
    for (const char* name : { "sub/m.txt", "a.txt", "z.txt" }) {
        b.Write(std::string("p/") + name, name);
    }
    auto da = ComputePackageDigest(a.Sub("p"));
    auto db = ComputePackageDigest(b.Sub("p"));
    ASSERT_TRUE(da);
    ASSERT_TRUE(db);
    EXPECT_EQ(da.Value(), db.Value());
    a.Write("p/.shard-install.json", "{}");
    a.Write("p/shard-signature.json", "{}");
    a.Write("p/.shard-extracted", "x");
    EXPECT_EQ(ComputePackageDigest(a.Sub("p")).Value(), da.Value());
    a.Write("p/a.txt", "changed");
    EXPECT_NE(ComputePackageDigest(a.Sub("p")).Value(), da.Value());
    EXPECT_FALSE(ComputePackageDigest(a.Sub("nowhere")));
}

TEST(PluginPackage, TrustStoreFilesAndKeyFiles)
{
    TempDir dir;
    Publisher acme(7);
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    ASSERT_TRUE(trust.SaveFile(dir.Sub("trust/plugin-trust.json")));
    TrustStore loaded;
    ASSERT_TRUE(loaded.LoadFile(dir.Sub("trust/plugin-trust.json")));
    EXPECT_TRUE(loaded.IsTrusted("acme", acme.pk));
    EXPECT_FALSE(loaded.IsTrusted("other", acme.pk));
    EXPECT_FALSE(loaded.LoadFile(dir.Sub("absent.json")));
    dir.Write("bad.json", R"({ "publishers": { "x": "nothex" } })");
    EXPECT_FALSE(TrustStore().LoadFile(dir.Sub("bad.json")));

    ASSERT_TRUE(WriteKeyPair(dir.Sub("keys/acme"), acme.sk, acme.pk));
    auto sk = ReadSecretKey(dir.Sub("keys/acme.key"));
    ASSERT_TRUE(sk) << sk.GetError().message;
    EXPECT_EQ(sk.Value(), acme.sk);
    EXPECT_EQ(dir.Read("keys/acme.pub"), Crypto::ToHex(acme.pk) + "\n");
    // a key whose public half is not the seed's
    std::string damaged = Crypto::ToHex(acme.sk);
    damaged[100] = damaged[100] == '0' ? '1' : '0';
    dir.Write("keys/damaged.key", damaged);
    EXPECT_FALSE(ReadSecretKey(dir.Sub("keys/damaged.key")));
    EXPECT_FALSE(ReadSecretKey(dir.Sub("keys/absent.key")));
}

// ---- Registry and installs ----

TEST(PluginRegistry, SearchAndFind)
{
    TempDir dir;
    Publisher acme(1);
    json index = NewIndex();
    MakePack(dir, "a1", "com.acme.weather", "1.0.0");
    MakePack(dir, "a2", "com.acme.weather", "1.2.0");
    MakePack(dir, "a3", "com.acme.weather", "2.0.0");
    MakePack(dir, "b", "com.acme.clouds", "0.5.0");
    Publish(dir, "a1", "reg", "com.acme.weather", "1.0.0", &acme, index);
    Publish(dir, "a2", "reg", "com.acme.weather", "1.2.0", &acme, index);
    Publish(dir, "a3", "reg", "com.acme.weather", "2.0.0", &acme, index);
    Publish(dir, "b", "reg", "com.acme.clouds", "0.5.0", &acme, index);
    dir.Write("reg/index.json", index.dump());

    Registry reg;
    ASSERT_TRUE(reg.Load(dir.Sub("reg")));
    EXPECT_EQ(reg.Name(), "Test registry");
    EXPECT_EQ(reg.Versions().size(), 4u);
    const auto found = reg.Search("weather");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].version.ToString(), "2.0.0");
    EXPECT_EQ(reg.Search("").size(), 2u);
    EXPECT_EQ(reg.Search("TEST PLUGIN").size(), 2u);
    EXPECT_TRUE(reg.Search("nothing like it").empty());

    const SemVer engine{ 0, 1, 0, {} };
    EXPECT_EQ(reg.Find("com.acme.weather", *VersionRange::Parse("^1.0"), engine)->version.ToString(), "1.2.0");
    EXPECT_FALSE(reg.Find("com.acme.weather", *VersionRange::Parse("^3.0"), engine));
    EXPECT_FALSE(reg.Find("nobody", VersionRange::Any(), engine));

    EXPECT_FALSE(Registry().Load(dir.Sub("absent")));
    dir.Write("badreg/index.json", R"({ "schema": 2 })");
    EXPECT_FALSE(Registry().Load(dir.Sub("badreg")));
}

TEST(PluginRegistry, InstallsASignedPluginAndItsDependencies)
{
    TempDir dir;
    Publisher acme(1);
    json index = NewIndex();
    MakePack(dir, "lib", "com.acme.lib", "1.0.0");
    MakePack(dir, "app", "com.acme.app", "1.0.0");
    Publish(dir, "lib", "reg", "com.acme.lib", "1.0.0", &acme, index);
    Publish(dir, "app", "reg", "com.acme.app", "1.0.0", &acme, index, json::array({ json{ { "id", "com.acme.lib" }, { "version", "^1.0" } }, json{ { "id", "shard.physics" } } }));
    dir.Write("reg/index.json", index.dump());

    Registry reg;
    ASSERT_TRUE(reg.Load(dir.Sub("reg")));
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    InstallOptions options;
    options.installDirectory = dir.Sub("project/plugins");
    options.engineVersion = SemVer{ 0, 1, 0, {} };
    options.trust = &trust;
    options.alreadyAvailable = { "shard.physics" };
    PluginInstaller installer({ reg });

    auto plan = installer.Plan("com.acme.app", VersionRange::Any(), options);
    ASSERT_TRUE(plan) << plan.GetError().message;
    ASSERT_EQ(plan.Value().size(), 2u);
    EXPECT_EQ(plan.Value()[0].id, "com.acme.lib");          // dependencies first

    auto installed = installer.Install("com.acme.app", VersionRange::Any(), options);
    ASSERT_TRUE(installed) << installed.GetError().message;
    ASSERT_EQ(installed.Value().size(), 2u);
    EXPECT_FALSE(dir.Read("project/plugins/com.acme.app/shard-plugin.json").empty());
    EXPECT_FALSE(dir.Read("project/plugins/com.acme.lib/content/tables/t.json").empty());

    InstallMarker marker;
    ASSERT_TRUE(ReadInstallMarker(dir.Sub("project/plugins/com.acme.app"), marker));
    EXPECT_EQ(marker.version, "1.0.0");
    EXPECT_EQ(marker.publisher, "acme");
    EXPECT_EQ(PluginInstaller::ListInstalled(options.installDirectory).size(), 2u);
    // what was installed still verifies, the marker being left out of the digest
    EXPECT_EQ(VerifyPackage(dir.Sub("project/plugins/com.acme.app"), trust).status, SignatureInfo::Status::Valid);

    // again : nothing is downloaded, nothing changes
    EXPECT_TRUE(installer.Install("com.acme.app", VersionRange::Any(), options));

    ASSERT_TRUE(PluginInstaller::Uninstall(options.installDirectory, "com.acme.app"));
    EXPECT_EQ(PluginInstaller::ListInstalled(options.installDirectory).size(), 1u);
    EXPECT_FALSE(PluginInstaller::Uninstall(options.installDirectory, "com.acme.app"));
    EXPECT_FALSE(PluginInstaller::Uninstall(options.installDirectory, "../escape"));
}

TEST(PluginRegistry, RefusesWhatIsNotWhatTheIndexSays)
{
    TempDir dir;
    Publisher acme(1);
    json index = NewIndex();
    MakePack(dir, "p", "com.acme.p", "1.0.0");
    Publish(dir, "p", "reg", "com.acme.p", "1.0.0", &acme, index);
    dir.Write("reg/index.json", index.dump());
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    InstallOptions options;
    options.installDirectory = dir.Sub("plugins");
    options.engineVersion = SemVer{ 0, 1, 0, {} };
    options.trust = &trust;

    // the archive is replaced after the index was written
    Registry reg;
    ASSERT_TRUE(reg.Load(dir.Sub("reg")));
    std::string archive = dir.Read("reg/com.acme.p-1.0.0.shardplugin");
    archive[archive.size() / 2] ^= 1;
    dir.Write("reg/com.acme.p-1.0.0.shardplugin", archive);
    auto tampered = PluginInstaller({ reg }).Install("com.acme.p", VersionRange::Any(), options);
    ASSERT_FALSE(tampered);
    EXPECT_NE(tampered.GetError().message.find("SHA-256"), std::string::npos) << tampered.GetError().message;
    EXPECT_FALSE(std::filesystem::exists(dir.Sub("plugins/com.acme.p")));

    // an unknown plugin and a range nothing satisfies
    EXPECT_FALSE(PluginInstaller({ reg }).Plan("com.acme.nobody", VersionRange::Any(), options));
    EXPECT_FALSE(PluginInstaller({ reg }).Plan("com.acme.p", *VersionRange::Parse("^2.0"), options));
}

TEST(PluginRegistry, UnsignedAndUntrustedPluginsNeedConsent)
{
    TempDir dir;
    Publisher acme(1);
    json index = NewIndex();
    MakePack(dir, "unsigned", "com.acme.unsigned", "1.0.0");
    MakePack(dir, "signed", "com.acme.signed", "1.0.0");
    Publish(dir, "unsigned", "reg", "com.acme.unsigned", "1.0.0", nullptr, index);
    Publish(dir, "signed", "reg", "com.acme.signed", "1.0.0", &acme, index);
    dir.Write("reg/index.json", index.dump());
    Registry reg;
    ASSERT_TRUE(reg.Load(dir.Sub("reg")));

    TrustStore empty, trust;
    trust.Trust("acme", acme.pk);
    InstallOptions options;
    options.installDirectory = dir.Sub("plugins");
    options.engineVersion = SemVer{ 0, 1, 0, {} };
    PluginInstaller installer({ reg });

    options.trust = &trust;
    auto unsignedInstall = installer.Install("com.acme.unsigned", VersionRange::Any(), options);
    ASSERT_FALSE(unsignedInstall);
    EXPECT_NE(unsignedInstall.GetError().message.find("unsigned"), std::string::npos);

    options.trust = &empty;
    auto untrusted = installer.Install("com.acme.signed", VersionRange::Any(), options);
    ASSERT_FALSE(untrusted);
    EXPECT_NE(untrusted.GetError().message.find("untrusted"), std::string::npos);

    options.allowUnsigned = true;
    EXPECT_TRUE(installer.Install("com.acme.signed", VersionRange::Any(), options));
    EXPECT_TRUE(installer.Install("com.acme.unsigned", VersionRange::Any(), options));
}

TEST(PluginRegistry, ABadSignatureIsNeverInstalledEvenWithConsent)
{
    TempDir dir;
    Publisher acme(1);
    json index = NewIndex();
    MakePack(dir, "p", "com.acme.p", "1.0.0");
    ASSERT_TRUE(SignPackage(dir.Sub("p"), "acme", acme.sk));
    dir.Write("p/content/tables/t.json", R"({"changed":true})");          // after signing
    Publish(dir, "p", "reg", "com.acme.p", "1.0.0", nullptr, index);
    dir.Write("reg/index.json", index.dump());
    Registry reg;
    ASSERT_TRUE(reg.Load(dir.Sub("reg")));
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    InstallOptions options;
    options.installDirectory = dir.Sub("plugins");
    options.engineVersion = SemVer{ 0, 1, 0, {} };
    options.trust = &trust;
    options.allowUnsigned = true;
    auto result = PluginInstaller({ reg }).Install("com.acme.p", VersionRange::Any(), options);
    ASSERT_FALSE(result);
    EXPECT_NE(result.GetError().message.find("not the ones that were signed"), std::string::npos) << result.GetError().message;
}

TEST(PluginRegistry, ReplacesAnOlderVersionAndFindsUpdates)
{
    TempDir dir;
    Publisher acme(1);
    json index = NewIndex();
    MakePack(dir, "v1", "com.acme.p", "1.0.0");
    Publish(dir, "v1", "reg", "com.acme.p", "1.0.0", &acme, index);
    dir.Write("reg/index.json", index.dump());
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    InstallOptions options;
    options.installDirectory = dir.Sub("plugins");
    options.engineVersion = SemVer{ 0, 1, 0, {} };
    options.trust = &trust;
    {
        Registry reg;
        ASSERT_TRUE(reg.Load(dir.Sub("reg")));
        ASSERT_TRUE(PluginInstaller({ reg }).Install("com.acme.p", VersionRange::Any(), options));
    }
    MakePack(dir, "v11", "com.acme.p", "1.1.0", R"({"a":11})");
    MakePack(dir, "v2", "com.acme.p", "2.0.0");
    Publish(dir, "v11", "reg", "com.acme.p", "1.1.0", &acme, index);
    Publish(dir, "v2", "reg", "com.acme.p", "2.0.0", &acme, index);
    dir.Write("reg/index.json", index.dump());

    Registry reg;
    ASSERT_TRUE(reg.Load(dir.Sub("reg")));
    PluginInstaller installer({ reg });
    const auto updates = installer.FindUpdates(options.installDirectory, options.engineVersion);
    ASSERT_EQ(updates.size(), 1u);
    EXPECT_EQ(updates[0].second.version.ToString(), "1.1.0");          // 2.0.0 is another major

    ASSERT_TRUE(installer.Install("com.acme.p", *VersionRange::Parse("^1.0"), options));
    EXPECT_EQ(dir.Read("plugins/com.acme.p/content/tables/t.json"), R"({"a":11})");
    EXPECT_TRUE(installer.FindUpdates(options.installDirectory, options.engineVersion).empty());
}

// ---- Fetching over http ----

TEST(PluginRegistryHttp, FetchesThroughRedirectsAndFailsCleanly)
{
    TestHttpServer server;
    server.SetBytes("/hello.txt", "hello over http");
    TestHttpServer::Route moved;
    moved.status = 302;
    moved.location = "/hello.txt";
    server.Set("/old", moved);
    TestHttpServer::Route cut;
    cut.body = { 'a', 'b', 'c' };
    cut.truncate = true;
    server.Set("/cut", cut);

    auto ok = FetchBytes(server.Url("/hello.txt"));
    ASSERT_TRUE(ok) << ok.GetError().message;
    EXPECT_EQ(std::string(ok.Value().begin(), ok.Value().end()), "hello over http");
    auto redirected = FetchBytes(server.Url("/old"));
    ASSERT_TRUE(redirected) << redirected.GetError().message;
    EXPECT_EQ(redirected.Value().size(), 15u);

    EXPECT_FALSE(FetchBytes(server.Url("/missing")));
    auto truncated = FetchBytes(server.Url("/cut"));
    ASSERT_FALSE(truncated);
    EXPECT_NE(truncated.GetError().message.find("truncated"), std::string::npos);
    auto big = FetchBytes(server.Url("/hello.txt"), 5);
    EXPECT_FALSE(big);
    auto https = FetchBytes("https://127.0.0.1/x");
    ASSERT_FALSE(https);
    EXPECT_NE(https.GetError().message.find("https"), std::string::npos);
    EXPECT_FALSE(FetchBytes("ftp://example.org/x"));
    EXPECT_FALSE(FetchBytes("http://127.0.0.1:1/never", 1000, 500));          // nothing listens there
}

TEST(PluginRegistryHttp, InstallsFromAnHttpRegistry)
{
    TempDir dir;
    Publisher acme(1);
    json index = NewIndex();
    MakePack(dir, "p", "com.acme.web", "1.0.0");
    Publish(dir, "p", "reg", "com.acme.web", "1.0.0", &acme, index);
    dir.Write("reg/index.json", index.dump());

    TestHttpServer server;
    server.SetBytes("/registry/index.json", dir.Read("reg/index.json"));
    TestHttpServer::Route archive;
    archive.body = FileBytes(dir, "reg/com.acme.web-1.0.0.shardplugin");
    server.Set("/registry/com.acme.web-1.0.0.shardplugin", archive);

    Registry reg;
    auto loaded = reg.Load(server.Url("/registry/index.json"));
    ASSERT_TRUE(loaded) << loaded.GetError().message;
    EXPECT_EQ(reg.ArchiveLocation(reg.Versions()[0]), server.Url("/registry/com.acme.web-1.0.0.shardplugin"));

    TrustStore trust;
    trust.Trust("acme", acme.pk);
    InstallOptions options;
    options.installDirectory = dir.Sub("project/plugins");
    options.engineVersion = SemVer{ 0, 1, 0, {} };
    options.trust = &trust;
    auto installed = PluginInstaller({ reg }).Install("com.acme.web", VersionRange::Any(), options);
    ASSERT_TRUE(installed) << installed.GetError().message;
    EXPECT_FALSE(dir.Read("project/plugins/com.acme.web/content/tables/t.json").empty());
}

// ---- The manager : archives, signatures, trust ----

namespace {
    class DistributionManagerTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            PluginManager::Get().ResetForTests();
            config.host = Host::Headless;
            config.registerConsoleCommands = false;
            config.readOnly = true;
        }
        void TearDown() override { PluginManager::Get().ResetForTests(); }
        PluginManager& pm() { return PluginManager::Get(); }
        bool Start(std::string* error = nullptr) { return pm().Startup(config, error); }
        bool Rejected(const std::string& id, Rejection::Kind kind)
        {
            for (const Rejection& r : pm().Rejections()) if (r.id == id && r.kind == kind) return true;
            return false;
        }
        PluginManagerConfig config;
    };
}

TEST_F(DistributionManagerTest, LoadsAPluginFromAnArchive)
{
    TempDir dir;
    MakePack(dir, "src", "pack.zipped", "1.0.0", R"({"zipped":true})");
    ZipWriter zip;
    ASSERT_TRUE(zip.AddDirectory(dir.Sub("src")));
    ASSERT_TRUE(zip.Save(dir.Sub("plugins/pack.zipped.shardplugin")));
    config.searchPaths.push_back(dir.Sub("plugins"));
    config.cacheDirectory = dir.Sub("cache");
    ASSERT_TRUE(Start());
    PluginRecord* r = pm().Find("pack.zipped");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->state, PluginState::Initialized) << r->error;
    EXPECT_EQ(pm().Content().LoadTable("t")["zipped"], true);
    EXPECT_TRUE(std::filesystem::exists(dir.Sub("cache")));

    // a second start reuses the unpacked folder
    pm().ResetForTests();
    ASSERT_TRUE(Start());
    EXPECT_NE(pm().Find("pack.zipped"), nullptr);
}

TEST_F(DistributionManagerTest, AnArchiveGivenWithPluginOrInTheProjectList)
{
    TempDir dir;
    MakePack(dir, "src", "pack.direct", "1.0.0");
    ZipWriter zip;
    ASSERT_TRUE(zip.AddDirectory(dir.Sub("src")));
    ASSERT_TRUE(zip.Save(dir.Sub("elsewhere/direct.shardplugin")));
    config.pluginPaths.push_back(dir.Sub("elsewhere/direct.shardplugin"));
    config.cacheDirectory = dir.Sub("cache");
    ASSERT_TRUE(Start());
    EXPECT_NE(pm().Find("pack.direct"), nullptr);

    pm().ResetForTests();
    config.pluginPaths.clear();
    dir.Write("proj/project.json", R"({ "plugins": [ { "id": "pack.direct", "path": "../elsewhere/direct.shardplugin" } ] })");
    config.projectFile = dir.Sub("proj/project.json");
    ASSERT_TRUE(Start());
    EXPECT_NE(pm().Find("pack.direct"), nullptr);
}

TEST_F(DistributionManagerTest, ABrokenArchiveIsReportedNotLoaded)
{
    TempDir dir;
    dir.Write("plugins/broken.shardplugin", "this is not a zip file at all, just text");
    config.searchPaths.push_back(dir.Sub("plugins"));
    config.cacheDirectory = dir.Sub("cache");
    ASSERT_TRUE(Start());
    bool reported = false;
    for (const Rejection& r : pm().Rejections()) if (r.reason.find("invalid archive") != std::string::npos) reported = true;
    EXPECT_TRUE(reported);
}

TEST_F(DistributionManagerTest, ASignedPluginIsRecordedAndATamperedOneIsRefused)
{
    TempDir dir;
    Publisher acme(1);
    MakePack(dir, "plugins/good", "pack.good", "1.0.0");
    MakePack(dir, "plugins/bad", "pack.bad", "1.0.0");
    ASSERT_TRUE(SignPackage(dir.Sub("plugins/good"), "acme", acme.sk));
    ASSERT_TRUE(SignPackage(dir.Sub("plugins/bad"), "acme", acme.sk));
    dir.Write("plugins/bad/content/tables/t.json", R"({"tampered":1})");
    TrustStore store;
    store.Trust("acme", acme.pk);
    ASSERT_TRUE(store.SaveFile(dir.Sub("trust.json")));
    config.searchPaths.push_back(dir.Sub("plugins"));
    config.trustFiles.push_back(dir.Sub("trust.json"));
    ASSERT_TRUE(Start());
    ASSERT_NE(pm().Find("pack.good"), nullptr);
    EXPECT_EQ(pm().Find("pack.good")->signature.status, SignatureInfo::Status::Valid);
    EXPECT_EQ(pm().Find("pack.bad"), nullptr);
    EXPECT_TRUE(Rejected("pack.bad", Rejection::Kind::Untrusted));
}

TEST_F(DistributionManagerTest, AnInstalledPluginMustCarryATrustedSignature)
{
    TempDir dir;
    Publisher acme(1), mallory(2);
    MakePack(dir, "plugins/installed.unsigned", "pack.i1", "1.0.0");
    MakePack(dir, "plugins/installed.untrusted", "pack.i2", "1.0.0");
    MakePack(dir, "plugins/installed.ok", "pack.i3", "1.0.0");
    ASSERT_TRUE(SignPackage(dir.Sub("plugins/installed.untrusted"), "mallory", mallory.sk));
    ASSERT_TRUE(SignPackage(dir.Sub("plugins/installed.ok"), "acme", acme.sk));
    for (const char* id : { "pack.i1", "pack.i2", "pack.i3" }) {
        const std::string folder = std::string("plugins/installed.") + (std::string(id) == "pack.i1" ? "unsigned" : std::string(id) == "pack.i2" ? "untrusted" : "ok");
        ASSERT_TRUE(WriteInstallMarker(dir.Sub(folder), InstallMarker{ id, "1.0.0", "test", "", "x" }));
    }
    TrustStore store;
    store.Trust("acme", acme.pk);
    ASSERT_TRUE(store.SaveFile(dir.Sub("trust.json")));
    config.searchPaths.push_back(dir.Sub("plugins"));
    config.trustFiles.push_back(dir.Sub("trust.json"));
    ASSERT_TRUE(Start());
    EXPECT_EQ(pm().Find("pack.i1"), nullptr);
    EXPECT_EQ(pm().Find("pack.i2"), nullptr);
    ASSERT_NE(pm().Find("pack.i3"), nullptr);
    EXPECT_TRUE(pm().Find("pack.i3")->installed);
    EXPECT_TRUE(Rejected("pack.i1", Rejection::Kind::Untrusted));
    EXPECT_TRUE(Rejected("pack.i2", Rejection::Kind::Untrusted));
}

TEST_F(DistributionManagerTest, NativePluginsNeedASignatureOrConsent)
{
    TempDir dir;
    Publisher acme(1);
    auto native = [&](const std::string& folder, const std::string& id) {
        json j = PluginTest::Manifest(id);
        j["kind"] = "module";
        j["moduleKind"] = "mod";
        j["linkage"] = "dynamic";
        j["tier"] = 1;
        j["binaries"] = { { "plan9-mips", "x.so" } };           // never loaded : only the trust decision is looked at
        dir.Write(folder + "/shard-plugin.json", j.dump());
    };
    native("plugins/unsigned", "native.unsigned");
    native("plugins/signed", "native.signed");
    ASSERT_TRUE(SignPackage(dir.Sub("plugins/signed"), "acme", acme.sk));
    TrustStore store;
    store.Trust("acme", acme.pk);
    ASSERT_TRUE(store.SaveFile(dir.Sub("trust.json")));
    config.searchPaths.push_back(dir.Sub("plugins"));
    config.trustFiles.push_back(dir.Sub("trust.json"));

    ASSERT_TRUE(Start());
    EXPECT_TRUE(Rejected("native.unsigned", Rejection::Kind::Untrusted));
    EXPECT_FALSE(Rejected("native.signed", Rejection::Kind::Untrusted));          // passed the trust check (and was left out for its platform)

    // the project's consent for one plugin
    pm().ResetForTests();
    dir.Write("proj/project.json", R"({ "plugins": [ { "id": "native.unsigned", "allowUnsigned": true } ] })");
    config.projectFile = dir.Sub("proj/project.json");
    config.trustFiles.clear();
    config.trustFiles.push_back(dir.Sub("trust.json"));
    ASSERT_TRUE(Start());
    EXPECT_FALSE(Rejected("native.unsigned", Rejection::Kind::Untrusted));

    // the command line's consent for all of them
    pm().ResetForTests();
    config.projectFile.clear();
    config.allowUnsigned = true;
    ASSERT_TRUE(Start());
    EXPECT_FALSE(Rejected("native.unsigned", Rejection::Kind::Untrusted));
}

TEST_F(DistributionManagerTest, InstallThroughTheManager)
{
    TempDir dir;
    Publisher acme(1);
    json index = NewIndex();
    MakePack(dir, "p", "com.acme.managed", "1.0.0");
    Publish(dir, "p", "reg", "com.acme.managed", "1.0.0", &acme, index);
    dir.Write("reg/index.json", index.dump());
    dir.Write("proj/project.json", R"({ "pluginsFolder": "plugins", "registries": [ ")" + dir.Sub("reg") + R"(" ] })");
    TrustStore store;
    store.Trust("acme", acme.pk);
    ASSERT_TRUE(store.SaveFile(dir.Sub("proj/plugin-trust.json")));
    config.projectFile = dir.Sub("proj/project.json");
    config.registerConsoleCommands = true;
    ASSERT_TRUE(Start());
    EXPECT_EQ(pm().Find("com.acme.managed"), nullptr);

    auto& console = Core::CVarRegistry::Global();
    EXPECT_NE(console.Execute("plugin.search managed").output.find("com.acme.managed"), std::string::npos);
    const auto installed = console.Execute("plugin.install com.acme.managed");
    EXPECT_TRUE(installed.ok) << installed.output;
    EXPECT_NE(console.Execute("plugin.installed").output.find("com.acme.managed"), std::string::npos);
    EXPECT_TRUE(console.Execute("plugin.updates").ok);

    // the next start loads it, as an installed (so signed) plugin
    pm().ResetForTests();
    config.registerConsoleCommands = false;
    ASSERT_TRUE(Start());
    PluginRecord* r = pm().Find("com.acme.managed");
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(r->installed);
    EXPECT_EQ(r->signature.status, SignatureInfo::Status::Valid);
    EXPECT_NE(pm().DescribePlugin("com.acme.managed").find("installed from a registry"), std::string::npos);

    // loaded : it cannot be removed under the running engine
    config.registerConsoleCommands = true;
    pm().ResetForTests();
    ASSERT_TRUE(Start());
    EXPECT_FALSE(console.Execute("plugin.uninstall com.acme.managed").ok);
    EXPECT_NE(console.Execute("plugin.verify com.acme.managed").output.find("valid"), std::string::npos);
}

TEST_F(DistributionManagerTest, TrustingAPublisherIsSavedForTheNextRun)
{
    TempDir dir;
    Publisher acme(1);
    dir.Write("proj/project.json", "{}");
    config.projectFile = dir.Sub("proj/project.json");
    ASSERT_TRUE(Start());
    ASSERT_TRUE(pm().TrustPublisher("acme", Crypto::ToHex(acme.pk)));
    EXPECT_TRUE(pm().Trust().IsTrusted("acme", acme.pk));
    EXPECT_FALSE(pm().TrustPublisher("acme", "short"));
    TrustStore reloaded;
    ASSERT_TRUE(reloaded.LoadFile(dir.Sub("proj/plugin-trust.json")));
    EXPECT_TRUE(reloaded.IsTrusted("acme", acme.pk));
}

#include <gtest/gtest.h>

#include <fstream>

#include "engine/plugins/archive.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/plugins/package.hpp"
#include "engine/plugins/installer.hpp"
#include "plugins/plugin_test_utils.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using nlohmann::json;
using PluginTest::TempDir;

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

    void MakePack(const TempDir& dir, const std::string& folder, const std::string& id, const std::string& version, const std::string& table = R"({"a":1})",
                  json extra = json::object())
    {
        dir.Write(folder + "/shard-plugin.json", PluginTest::DatapackManifest(id, version, extra));
        dir.Write(folder + "/content/tables/t.json", table);
    }

    std::string Pack(const TempDir& dir, const std::string& folder, const std::string& outFolder, const std::string& id, const std::string& version, const Publisher* signer)
    {
        if (signer)
        {
            EXPECT_TRUE(SignPackage(dir.Sub(folder), "acme", signer->sk));
        }
        ZipWriter zip;
        EXPECT_TRUE(zip.AddDirectory(dir.Sub(folder)));
        const std::string file = outFolder + "/" + id + "-" + version + std::string(kArchiveExtension);
        const std::vector<uint8_t> bytes = zip.Finish();
        dir.Write(file, std::string(bytes.begin(), bytes.end()));
        return dir.Sub(file);
    }

    std::string Sha256OfFile(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return Crypto::ToHex(Crypto::Sha256Of(bytes.data(), bytes.size()));
    }

    InstallOptions Options(const TempDir& dir, const TrustStore& trust)
    {
        InstallOptions options;
        options.installDirectory = dir.Sub("project/plugins");
        options.engineVersion = SemVer{ 0, 1, 0, {} };
        options.trust = &trust;
        return options;
    }
}

TEST(PluginPackage, SignedFolderVerifies)
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

    dir.Write("p/content/tables/t.json", R"({"a":2})");
    EXPECT_EQ(VerifyPackage(dir.Sub("p"), trust).status, SignatureInfo::Status::Invalid);
    dir.Write("p/content/tables/t.json", R"({"a":1})");
    EXPECT_EQ(VerifyPackage(dir.Sub("p"), trust).status, SignatureInfo::Status::Valid);

    dir.Write("p/content/extra.txt", "x");
    EXPECT_EQ(VerifyPackage(dir.Sub("p"), trust).status, SignatureInfo::Status::Invalid);
    std::filesystem::remove(dir.Sub("p/content/extra.txt"));

    // The signature covers id and version, so the same files under another version are unsigned.
    dir.Write("p/shard-plugin.json", PluginTest::DatapackManifest("pack.signed", "1.0.1"));
    EXPECT_EQ(VerifyPackage(dir.Sub("p"), trust).status, SignatureInfo::Status::Invalid);
}

TEST(PluginPackage, ForgedSignatureInvalid)
{
    TempDir dir;
    Publisher acme(1), mallory(2);
    MakePack(dir, "p", "pack.forged", "1.0.0");
    ASSERT_TRUE(SignPackage(dir.Sub("p"), "acme", acme.sk));
    TrustStore trust;
    trust.Trust("acme", acme.pk);

    MakePack(dir, "q", "pack.forged", "1.0.0");
    ASSERT_TRUE(SignPackage(dir.Sub("q"), "acme", mallory.sk));
    SignatureInfo forged = VerifyPackage(dir.Sub("q"), trust);
    EXPECT_EQ(forged.status, SignatureInfo::Status::Untrusted) << "the signature is real, but the key is not the trusted acme's";

    json sig = json::parse(dir.Read("q/shard-signature.json"));
    sig["publicKey"] = Crypto::ToHex(acme.pk);
    dir.Write("q/shard-signature.json", sig.dump());
    EXPECT_EQ(VerifyPackage(dir.Sub("q"), trust).status, SignatureInfo::Status::Invalid);

    dir.Write("q/shard-signature.json", "not json");
    EXPECT_EQ(VerifyPackage(dir.Sub("q"), trust).status, SignatureInfo::Status::Invalid);
}

TEST(PluginPackage, RevokedKeyRefused)
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

TEST(PluginPackage, DigestIgnoresFileSystem)
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
    std::string damaged = Crypto::ToHex(acme.sk);
    damaged[100] = damaged[100] == '0' ? '1' : '0';
    dir.Write("keys/damaged.key", damaged);
    EXPECT_FALSE(ReadSecretKey(dir.Sub("keys/damaged.key")));
    EXPECT_FALSE(ReadSecretKey(dir.Sub("keys/absent.key")));
}

TEST(PluginInstaller, InstallsSignedArchive)
{
    TempDir dir;
    Publisher acme(1);
    MakePack(dir, "app", "com.acme.app", "1.0.0");
    const std::string archive = Pack(dir, "app", "dl", "com.acme.app", "1.0.0", &acme);
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    const InstallOptions options = Options(dir, trust);

    auto installed = PluginInstaller::InstallArchive(archive, options);
    ASSERT_TRUE(installed) << installed.GetError().message;
    EXPECT_EQ(installed.Value().id, "com.acme.app");
    EXPECT_EQ(installed.Value().publisher, "acme");
    EXPECT_FALSE(dir.Read("project/plugins/com.acme.app/content/tables/t.json").empty());

    InstallMarker marker;
    ASSERT_TRUE(ReadInstallMarker(dir.Sub("project/plugins/com.acme.app"), marker));
    EXPECT_EQ(marker.version, "1.0.0");
    EXPECT_EQ(marker.source, archive);
    EXPECT_EQ(PluginInstaller::ListInstalled(options.installDirectory).size(), 1u);
    EXPECT_EQ(VerifyPackage(dir.Sub("project/plugins/com.acme.app"), trust).status, SignatureInfo::Status::Valid);

    ASSERT_TRUE(PluginInstaller::Uninstall(options.installDirectory, "com.acme.app"));
    EXPECT_TRUE(PluginInstaller::ListInstalled(options.installDirectory).empty());
    EXPECT_FALSE(PluginInstaller::Uninstall(options.installDirectory, "com.acme.app"));
    EXPECT_FALSE(PluginInstaller::Uninstall(options.installDirectory, "../escape"));
}

TEST(PluginInstaller, RefusesWrongChecksumAndMissingFile)
{
    TempDir dir;
    Publisher acme(1);
    MakePack(dir, "p", "com.acme.p", "1.0.0");
    const std::string archive = Pack(dir, "p", "dl", "com.acme.p", "1.0.0", &acme);
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    InstallOptions options = Options(dir, trust);

    options.expectedSha256 = std::string(64, '0');
    auto wrong = PluginInstaller::InstallArchive(archive, options);
    ASSERT_FALSE(wrong);
    EXPECT_NE(wrong.GetError().message.find("SHA-256"), std::string::npos) << wrong.GetError().message;
    EXPECT_FALSE(std::filesystem::exists(dir.Sub("project/plugins/com.acme.p")));

    options.expectedSha256 = Sha256OfFile(archive);
    EXPECT_TRUE(PluginInstaller::InstallArchive(archive, options));
    EXPECT_FALSE(PluginInstaller::InstallArchive(dir.Sub("dl/absent.shardplugin"), Options(dir, trust)));

    dir.Write("dl/junk.shardplugin", "this is not a zip");
    EXPECT_FALSE(PluginInstaller::InstallArchive(dir.Sub("dl/junk.shardplugin"), Options(dir, trust)));
}

TEST(PluginInstaller, UnsignedNeedsConsent)
{
    TempDir dir;
    Publisher acme(1), mallory(2);
    MakePack(dir, "unsigned", "com.acme.unsigned", "1.0.0");
    MakePack(dir, "signed", "com.acme.signed", "1.0.0");
    const std::string unsignedArchive = Pack(dir, "unsigned", "dl", "com.acme.unsigned", "1.0.0", nullptr);
    const std::string signedArchive = Pack(dir, "signed", "dl", "com.acme.signed", "1.0.0", &mallory);
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    InstallOptions options = Options(dir, trust);

    auto unsignedInstall = PluginInstaller::InstallArchive(unsignedArchive, options);
    ASSERT_FALSE(unsignedInstall);
    EXPECT_NE(unsignedInstall.GetError().message.find("not installed"), std::string::npos);
    EXPECT_FALSE(PluginInstaller::InstallArchive(signedArchive, options));
    EXPECT_TRUE(PluginInstaller::ListInstalled(options.installDirectory).empty());

    trust.Trust("acme", mallory.pk);
    EXPECT_TRUE(PluginInstaller::InstallArchive(signedArchive, options));
    options.allowUnsigned = true;
    EXPECT_TRUE(PluginInstaller::InstallArchive(unsignedArchive, options));
}

TEST(PluginInstaller, BadSignatureNeverInstalled)
{
    TempDir dir;
    Publisher acme(1);
    MakePack(dir, "p", "com.acme.p", "1.0.0");
    ASSERT_TRUE(SignPackage(dir.Sub("p"), "acme", acme.sk));
    dir.Write("p/content/tables/t.json", R"({"tampered":1})");
    const std::string archive = Pack(dir, "p", "dl", "com.acme.p", "1.0.0", nullptr);
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    InstallOptions options = Options(dir, trust);
    options.allowUnsigned = true;
    EXPECT_FALSE(PluginInstaller::InstallArchive(archive, options));
    EXPECT_FALSE(std::filesystem::exists(dir.Sub("project/plugins/com.acme.p")));
}

TEST(PluginInstaller, ChecksEngineAndApi)
{
    TempDir dir;
    Publisher acme(1);
    MakePack(dir, "future", "com.acme.future", "1.0.0", R"({"a":1})", json{ { "engine", { { "versions", ">=9.0.0" } } } });
    MakePack(dir, "api2", "com.acme.api2", "1.0.0", R"({"a":1})", json{ { "engine", { { "api", "2.0" } } } });
    const std::string future = Pack(dir, "future", "dl", "com.acme.future", "1.0.0", &acme);
    const std::string api2 = Pack(dir, "api2", "dl", "com.acme.api2", "1.0.0", &acme);
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    const InstallOptions options = Options(dir, trust);
    auto tooNew = PluginInstaller::InstallArchive(future, options);
    ASSERT_FALSE(tooNew);
    EXPECT_NE(tooNew.GetError().message.find("engine"), std::string::npos) << tooNew.GetError().message;
    auto otherApi = PluginInstaller::InstallArchive(api2, options);
    ASSERT_FALSE(otherApi);
    EXPECT_NE(otherApi.GetError().message.find("API"), std::string::npos) << otherApi.GetError().message;
}

TEST(PluginInstaller, ReplacesOlderVersion)
{
    TempDir dir;
    Publisher acme(1);
    MakePack(dir, "v1", "com.acme.p", "1.0.0");
    MakePack(dir, "v11", "com.acme.p", "1.1.0", R"({"a":11})");
    const std::string first = Pack(dir, "v1", "dl", "com.acme.p", "1.0.0", &acme);
    const std::string second = Pack(dir, "v11", "dl", "com.acme.p", "1.1.0", &acme);
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    const InstallOptions options = Options(dir, trust);
    ASSERT_TRUE(PluginInstaller::InstallArchive(first, options));
    ASSERT_TRUE(PluginInstaller::InstallArchive(second, options));
    EXPECT_EQ(dir.Read("project/plugins/com.acme.p/content/tables/t.json"), R"({"a":11})");
    ASSERT_EQ(PluginInstaller::ListInstalled(options.installDirectory).size(), 1u);
    EXPECT_EQ(PluginInstaller::ListInstalled(options.installDirectory)[0].version, "1.1.0");
}

namespace {
    void MakeBundle(const TempDir& dir, Publisher& signer)
    {
        MakePack(dir, "lib", "com.acme.lib", "1.0.0");
        MakePack(dir, "lib2", "com.acme.lib", "1.2.0");
        MakePack(dir, "app", "com.acme.app", "1.0.0", R"({"a":1})",
                 json{ { "dependencies", json::array({ json{ { "id", "com.acme.lib" }, { "version", "^1.0" } }, json{ { "id", "shard.physics" } } }) } });
        MakePack(dir, "solo", "com.acme.solo", "1.0.0");
        Pack(dir, "lib", "bundle", "com.acme.lib", "1.0.0", &signer);
        Pack(dir, "lib2", "bundle", "com.acme.lib", "1.2.0", &signer);
        Pack(dir, "app", "bundle", "com.acme.app", "1.0.0", &signer);
        Pack(dir, "solo", "bundle", "com.acme.solo", "1.0.0", &signer);
    }
}

TEST(PluginInstaller, BundleInstallsEverythingOrWhatIsAskedWithDependencies)
{
    TempDir dir;
    Publisher acme(1);
    MakeBundle(dir, acme);
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    InstallOptions options = Options(dir, trust);
    options.alreadyAvailable = { "shard.physics" };

    auto read = PluginInstaller::ReadBundle(dir.Sub("bundle"), options);
    ASSERT_TRUE(read) << read.GetError().message;
    ASSERT_EQ(read.Value().size(), 3u);

    auto plan = PluginInstaller::PlanBundle(dir.Sub("bundle"), { "com.acme.app" }, options);
    ASSERT_TRUE(plan) << plan.GetError().message;
    ASSERT_EQ(plan.Value().size(), 2u);
    EXPECT_EQ(plan.Value()[0].id, "com.acme.lib");
    EXPECT_EQ(plan.Value()[0].version.ToString(), "1.2.0");
    EXPECT_EQ(plan.Value()[1].id, "com.acme.app");

    auto some = PluginInstaller::InstallBundle(dir.Sub("bundle"), { "com.acme.app" }, options);
    ASSERT_TRUE(some) << some.GetError().message;
    EXPECT_EQ(some.Value().size(), 2u);
    EXPECT_EQ(PluginInstaller::ListInstalled(options.installDirectory).size(), 2u);
    EXPECT_FALSE(std::filesystem::exists(dir.Sub("project/plugins/com.acme.solo")));

    auto all = PluginInstaller::InstallBundle(dir.Sub("bundle"), {}, options);
    ASSERT_TRUE(all) << all.GetError().message;
    EXPECT_EQ(all.Value().size(), 3u);
    EXPECT_EQ(PluginInstaller::ListInstalled(options.installDirectory).size(), 3u);

    EXPECT_FALSE(PluginInstaller::PlanBundle(dir.Sub("bundle"), { "com.acme.nobody" }, options));
    EXPECT_FALSE(PluginInstaller::ReadBundle(dir.Sub("bundle/absent"), options));
}

TEST(PluginInstaller, BundleMissingDependencyRefused)
{
    TempDir dir;
    Publisher acme(1);
    MakePack(dir, "app", "com.acme.app", "1.0.0", R"({"a":1})", json{ { "dependencies", json::array({ json{ { "id", "com.acme.lib" } } }) } });
    Pack(dir, "app", "bundle", "com.acme.app", "1.0.0", &acme);
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    const InstallOptions options = Options(dir, trust);
    auto plan = PluginInstaller::PlanBundle(dir.Sub("bundle"), {}, options);
    ASSERT_FALSE(plan);
    EXPECT_NE(plan.GetError().message.find("com.acme.lib"), std::string::npos);
}

TEST(PluginInstaller, BundleFromASingleArchive)
{
    TempDir dir;
    Publisher acme(1);
    MakeBundle(dir, acme);
    ZipWriter bundle;
    ASSERT_TRUE(bundle.AddDirectory(dir.Sub("bundle")));
    ASSERT_TRUE(bundle.Save(dir.Sub("all.zip")));
    TrustStore trust;
    trust.Trust("acme", acme.pk);
    InstallOptions options = Options(dir, trust);
    options.alreadyAvailable = { "shard.physics" };
    auto installed = PluginInstaller::InstallBundle(dir.Sub("all.zip"), { "com.acme.solo" }, options);
    ASSERT_TRUE(installed) << installed.GetError().message;
    ASSERT_EQ(installed.Value().size(), 1u);
    EXPECT_FALSE(dir.Read("project/plugins/com.acme.solo/content/tables/t.json").empty());
}

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

TEST_F(DistributionManagerTest, LoadsFromArchive)
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

    pm().ResetForTests();
    ASSERT_TRUE(Start());
    EXPECT_NE(pm().Find("pack.zipped"), nullptr);
}

TEST_F(DistributionManagerTest, ArchiveViaPluginOrProjectList)
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

TEST_F(DistributionManagerTest, BrokenArchiveReported)
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

TEST_F(DistributionManagerTest, SignedRecordedTamperedRefused)
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

TEST_F(DistributionManagerTest, InstalledNeedsTrustedSignature)
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

TEST_F(DistributionManagerTest, NativeNeedsSignatureOrConsent)
{
    TempDir dir;
    Publisher acme(1);
    auto native = [&](const std::string& folder, const std::string& id) {
        json j = PluginTest::Manifest(id);
        j["kind"] = "module";
        j["moduleKind"] = "mod";
        j["linkage"] = "dynamic";
        j["tier"] = 1;
        j["binaries"] = { { "plan9-mips", "x.so" } };
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
    EXPECT_FALSE(Rejected("native.signed", Rejection::Kind::Untrusted));

    pm().ResetForTests();
    dir.Write("proj/project.json", R"({ "plugins": [ { "id": "native.unsigned", "allowUnsigned": true } ] })");
    config.projectFile = dir.Sub("proj/project.json");
    config.trustFiles.clear();
    config.trustFiles.push_back(dir.Sub("trust.json"));
    ASSERT_TRUE(Start());
    EXPECT_FALSE(Rejected("native.unsigned", Rejection::Kind::Untrusted));

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
    MakePack(dir, "p", "com.acme.managed", "1.0.0");
    const std::string archive = Pack(dir, "p", "dl", "com.acme.managed", "1.0.0", &acme);
    dir.Write("proj/project.json", R"({ "pluginsFolder": "plugins" })");
    TrustStore store;
    store.Trust("acme", acme.pk);
    ASSERT_TRUE(store.SaveFile(dir.Sub("proj/plugin-trust.json")));
    config.projectFile = dir.Sub("proj/project.json");
    config.registerConsoleCommands = true;
    ASSERT_TRUE(Start());
    EXPECT_EQ(pm().Find("com.acme.managed"), nullptr);

    auto& console = Core::CVarRegistry::Global();
    const auto installed = console.Execute("plugin.install " + archive);
    EXPECT_TRUE(installed.ok) << installed.output;
    EXPECT_NE(console.Execute("plugin.installed").output.find("com.acme.managed"), std::string::npos);
    EXPECT_FALSE(console.Execute("plugin.install " + dir.Sub("dl/absent.shardplugin")).ok);
    EXPECT_FALSE(console.Execute("plugin.search managed").ok);

    pm().ResetForTests();
    config.registerConsoleCommands = false;
    ASSERT_TRUE(Start());
    PluginRecord* r = pm().Find("com.acme.managed");
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(r->installed);
    EXPECT_EQ(r->signature.status, SignatureInfo::Status::Valid);
    EXPECT_NE(pm().DescribePlugin("com.acme.managed").find("installed"), std::string::npos);

    config.registerConsoleCommands = true;
    pm().ResetForTests();
    ASSERT_TRUE(Start());
    EXPECT_FALSE(console.Execute("plugin.uninstall com.acme.managed").ok);
    EXPECT_NE(console.Execute("plugin.verify com.acme.managed").output.find("valid"), std::string::npos);
}

TEST_F(DistributionManagerTest, InstallBundleThroughTheManager)
{
    TempDir dir;
    Publisher acme(1);
    MakePack(dir, "a", "com.acme.a", "1.0.0");
    MakePack(dir, "b", "com.acme.b", "1.0.0");
    Pack(dir, "a", "bundle", "com.acme.a", "1.0.0", &acme);
    Pack(dir, "b", "bundle", "com.acme.b", "1.0.0", &acme);
    dir.Write("proj/project.json", R"({ "pluginsFolder": "plugins" })");
    TrustStore store;
    store.Trust("acme", acme.pk);
    ASSERT_TRUE(store.SaveFile(dir.Sub("proj/plugin-trust.json")));
    config.projectFile = dir.Sub("proj/project.json");
    config.registerConsoleCommands = true;
    ASSERT_TRUE(Start());

    auto& console = Core::CVarRegistry::Global();
    const auto one = console.Execute("plugin.install-bundle " + dir.Sub("bundle") + " com.acme.a");
    EXPECT_TRUE(one.ok) << one.output;
    EXPECT_EQ(console.Execute("plugin.installed").output.find("com.acme.b"), std::string::npos);
    EXPECT_TRUE(console.Execute("plugin.install-bundle " + dir.Sub("bundle")).ok);
    EXPECT_NE(console.Execute("plugin.installed").output.find("com.acme.b"), std::string::npos);
}

TEST_F(DistributionManagerTest, TrustedPublisherSaved)
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

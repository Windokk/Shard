#include <fstream>

#include <gtest/gtest.h>

#include "engine/assets/assetID.hpp"
#include "engine/assets/pak/pak.hpp"
#include "engine/assets/vfs/filesystem.hpp"
#include "engine/plugins/content.hpp"
#include "engine/plugins/module_assets.hpp"
#include "plugins/plugin_test_utils.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using nlohmann::json;
using PluginTest::TempDir;

namespace {
    PluginManifest ModuleAt(const TempDir& dir, const std::string& folder, const std::string& id)
    {
        json j = PluginTest::Manifest(id);
        j["kind"] = "module";
        j["moduleKind"] = "mod";
        j["content"] = { { "mount", "content" } };
        PluginManifest m = ParseManifest(j).Value();
        m.rootDir = dir.Sub(folder);
        return m;
    }
}

TEST(Pak, Crc32MatchesTheKnownValue)
{
    EXPECT_EQ(Pak::Crc32("123456789", 9), 0xCBF43926u);
    EXPECT_EQ(Pak::Crc32("", 0), 0u);
}

TEST(Pak, WritesAndReadsBack)
{
    TempDir dir;
    Pak::PakWriter writer;
    ASSERT_TRUE(writer.Add("materials/stone.mat", "stone"));
    ASSERT_TRUE(writer.Add("\\tables\\items.json", "{}"));
    ASSERT_TRUE(writer.Add("empty.bin", ""));
    std::string err;
    ASSERT_TRUE(writer.Write(dir.Sub("a.shardpak"), &err)) << err;

    Pak::PakReader reader;
    ASSERT_TRUE(reader.Open(dir.Sub("a.shardpak"), &err)) << err;
    EXPECT_EQ(reader.Entries().size(), 3u);
    EXPECT_EQ(*reader.Read("materials/stone.mat"), "stone");
    EXPECT_EQ(*reader.Read("tables/items.json"), "{}");
    EXPECT_EQ(*reader.Read("empty.bin"), "");
    EXPECT_FALSE(reader.Read("missing.txt"));
    EXPECT_EQ(reader.List("tables"), (std::vector<std::string>{ "tables/items.json" }));
    EXPECT_TRUE(reader.Verify().empty());
}

TEST(Pak, SameInputGivesTheSameBytes)
{
    Pak::PakWriter a, b;
    a.Add("x/1.txt", "one");
    a.Add("a/2.txt", "two");
    b.Add("a/2.txt", "two");
    b.Add("x/1.txt", "one");
    EXPECT_EQ(a.ToBytes(), b.ToBytes());
}

TEST(Pak, RefusesUnsafePaths)
{
    Pak::PakWriter writer;
    EXPECT_FALSE(writer.Add("../secret.txt", "x"));
    EXPECT_FALSE(writer.Add("a/../b.txt", "x"));
    EXPECT_FALSE(Pak::IsSafePath("a\\..\\b.txt"));
    EXPECT_FALSE(writer.Add("C:/x.txt", "x"));
    EXPECT_FALSE(writer.Add("", "x"));
    EXPECT_FALSE(writer.Add("a//b.txt", "x"));
    EXPECT_EQ(writer.Count(), 0u);
}

TEST(Pak, DetectsDamage)
{
    TempDir dir;
    Pak::PakWriter writer;
    writer.Add("a.txt", "hello world");
    ASSERT_TRUE(writer.Write(dir.Sub("a.shardpak")));

    std::string bytes = dir.Read("a.shardpak");
    bytes[32] ^= 0x55; // first byte of the data
    dir.Write("bad.shardpak", bytes);
    Pak::PakReader bad;
    ASSERT_TRUE(bad.Open(dir.Sub("bad.shardpak")));
    EXPECT_FALSE(bad.Read("a.txt"));
    EXPECT_EQ(bad.Verify(), (std::vector<std::string>{ "a.txt" }));

    dir.Write("short.shardpak", bytes.substr(0, 20));
    std::string err;
    Pak::PakReader shortPak;
    EXPECT_FALSE(shortPak.Open(dir.Sub("short.shardpak"), &err));
    EXPECT_FALSE(err.empty());

    dir.Write("magic.shardpak", "NOPE" + bytes.substr(4));
    Pak::PakReader magic;
    EXPECT_FALSE(magic.Open(dir.Sub("magic.shardpak")));

    // A table of contents that points outside the file
    std::string cut = bytes;
    cut.resize(cut.size() - 10);
    dir.Write("cut.shardpak", cut);
    Pak::PakReader truncated;
    EXPECT_FALSE(truncated.Open(dir.Sub("cut.shardpak")));
}

TEST(Pak, PacksAFolder)
{
    TempDir dir;
    dir.Write("src/a/b.txt", "b");
    dir.Write("src/c.txt", "c");
    Pak::PakWriter writer;
    EXPECT_EQ(writer.AddFolder(dir.Sub("src"), "pre"), 2u);
    ASSERT_TRUE(writer.Write(dir.Sub("out/p.shardpak")));
    Pak::PakReader reader;
    ASSERT_TRUE(reader.Open(dir.Sub("out/p.shardpak")));
    EXPECT_EQ(*reader.Read("pre/a/b.txt"), "b");
    EXPECT_EQ(*reader.Read("pre/c.txt"), "c");
}

TEST(ContentMountsPak, MountsACookedModuleLikeAFolder)
{
    TempDir dir;
    dir.Write("src/materials/stone.mat", "stone");
    dir.Write("src/tables/items.json", R"({"sword":{"damage":3}})");
    dir.Write("src/lang/en.json", R"({"hello":"Hello"})");
    Pak::PakWriter writer;
    ASSERT_EQ(writer.AddFolder(dir.Sub("src")), 3u);
    ASSERT_TRUE(writer.Write(dir.Sub("one/content.shardpak")));

    ContentMounts mounts;
    mounts.SetCacheRoot(dir.Sub("cache"));
    std::string err;
    ASSERT_TRUE(mounts.Mount(ModuleAt(dir, "one", "mod.one"), 1, &err)) << err;
    ASSERT_TRUE(mounts.Find("mod.one")->pak);

    EXPECT_TRUE(mounts.Exists("mod://mod.one/materials/stone.mat"));
    EXPECT_EQ(*mounts.ReadText("mod://mod.one/materials/stone.mat"), "stone");
    EXPECT_EQ(mounts.List("mod.one", "tables"), (std::vector<std::string>{ "tables/items.json" }));
    EXPECT_EQ(mounts.LoadTable("items")["sword"]["damage"], 3);
    EXPECT_EQ(mounts.Translate("hello", "en"), "Hello");
    EXPECT_EQ(mounts.ResolveLogical("materials/stone.mat")->pluginId, "mod.one");
    EXPECT_FALSE(mounts.ReadText("mod://mod.one/../secret.txt"));

    // A caller that wants a real file gets one, unpacked once
    const auto file = mounts.Resolve("mod://mod.one/materials/stone.mat");
    ASSERT_TRUE(file);
    std::ifstream in(*file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text, "stone");
}

TEST(ContentMountsPak, AFolderWinsOverAPakAndABadPakFails)
{
    TempDir dir;
    dir.Write("one/content/a.txt", "folder");
    Pak::PakWriter writer;
    writer.Add("a.txt", "pak");
    ASSERT_TRUE(writer.Write(dir.Sub("one/content.shardpak")));
    ContentMounts mounts;
    ASSERT_TRUE(mounts.Mount(ModuleAt(dir, "one", "mod.one"), 1));
    EXPECT_FALSE(mounts.Find("mod.one")->pak);
    EXPECT_EQ(*mounts.ReadText("mod://mod.one/a.txt"), "folder");

    dir.Write("two/content.shardpak", "this is not a pak at all, not even close to one");
    ContentMounts other;
    std::string err;
    EXPECT_FALSE(other.Mount(ModuleAt(dir, "two", "mod.two"), 2, &err));
    EXPECT_FALSE(err.empty());
}

TEST(ContentMountsPak, TwoPaksLayerAndUnmountDropsThem)
{
    TempDir dir;
    Pak::PakWriter a, b;
    a.Add("x.txt", "from a");
    b.Add("x.txt", "from b");
    ASSERT_TRUE(a.Write(dir.Sub("a/content.shardpak")));
    ASSERT_TRUE(b.Write(dir.Sub("b/content.shardpak")));
    ContentMounts mounts;
    ASSERT_TRUE(mounts.Mount(ModuleAt(dir, "a", "mod.a"), 1));
    ASSERT_TRUE(mounts.Mount(ModuleAt(dir, "b", "mod.b"), 2));
    ASSERT_EQ(mounts.FindConflicts().size(), 1u);
    EXPECT_EQ(mounts.ResolveLogical("x.txt")->pluginId, "mod.a");
    EXPECT_EQ(mounts.Unmount(1), 1u);
    EXPECT_EQ(mounts.ResolveLogical("x.txt")->pluginId, "mod.b");
}

TEST(ModuleAssets, FilesGetStableIdsInTheDatabase)
{
    TempDir dir;
    dir.Write("one/content/materials/stone.mat", "stone");
    dir.Write("one/content/textures/rock.png", "png");
    ContentMounts mounts;
    ASSERT_TRUE(mounts.Mount(ModuleAt(dir, "one", "mod.one"), 1));

    Filesystem::AssetIDManager ids;
    ids.InitCounter(Filesystem::kFirstProjectAssetID);
    EXPECT_EQ(RegisterModuleAssets(mounts, "mod.one", ids), 2u);

    const Filesystem::AssetID stone = ids.GetIDFromNameInProject("mod://mod.one/materials/stone.mat");
    ASSERT_TRUE(ids.GetAssetFromID(stone));
    EXPECT_TRUE(Filesystem::IsModuleAssetID(stone));
    EXPECT_EQ(ids.GetAssetFromID(stone)->baseInfos.nameInProject, "mod://mod.one/materials/stone.mat");

    // Same names, same ids, on another database
    Filesystem::AssetIDManager again;
    again.InitCounter(Filesystem::kFirstProjectAssetID);
    RegisterModuleAssets(mounts, "mod.one", again);
    EXPECT_EQ(again.GetIDFromNameInProject("mod://mod.one/materials/stone.mat"), stone);

    // Registering twice does not duplicate, and project ids are untouched
    EXPECT_EQ(RegisterModuleAssets(mounts, "mod.one", ids), 2u);
    EXPECT_EQ(ids.AssetIDMap.size(), 2u);
    EXPECT_EQ(ids.GenerateNewID().GetAsInt(), Filesystem::kFirstProjectAssetID);

    EXPECT_EQ(UnregisterModuleAssets("mod.one", ids), 2u);
    EXPECT_TRUE(ids.AssetIDMap.empty());
}

TEST(ModuleAssets, PakAssetsKeepTheirSize)
{
    TempDir dir;
    Pak::PakWriter writer;
    writer.Add("sounds/boom.wav", "12345678");
    ASSERT_TRUE(writer.Write(dir.Sub("one/content.shardpak")));
    ContentMounts mounts;
    ASSERT_TRUE(mounts.Mount(ModuleAt(dir, "one", "mod.one"), 1));
    Filesystem::AssetIDManager ids;
    ids.InitCounter(Filesystem::kFirstProjectAssetID);
    ASSERT_EQ(RegisterModuleAssets(mounts, "mod.one", ids), 1u);
    const auto infos = ids.GetAssetFromID(ids.GetIDFromNameInProject("mod://mod.one/sounds/boom.wav"));
    ASSERT_TRUE(infos);
    EXPECT_EQ(infos->baseInfos.size, 8);
}

TEST(PakCompression, RoundTripsText)
{
    std::string text;
    for (int i = 0; i < 400; ++i) text += "{\"name\":\"actor" + std::to_string(i % 7) + "\",\"position\":[0,1,2]},\n";
    const std::string packed = Pak::Compress(text);
    EXPECT_LT(packed.size(), text.size() / 3);
    const auto plain = Pak::Decompress(packed.data(), packed.size(), text.size());
    ASSERT_TRUE(plain);
    EXPECT_EQ(*plain, text);
}

TEST(PakCompression, RoundTripsOddInputs)
{
    std::vector<std::string> inputs = { "", "a", "abcdefghijkl", std::string(13, 'x'), std::string(100000, 'z'), std::string(300, 'a') + "b" + std::string(300, 'a') };
    std::string noise;
    unsigned s = 12345;
    for (int i = 0; i < 70000; ++i) { s = s * 1664525u + 1013904223u; noise.push_back(char(s >> 24)); }
    inputs.push_back(noise);
    for (const std::string& in : inputs) {
        const std::string packed = Pak::Compress(in);
        const auto plain = Pak::Decompress(packed.data(), packed.size(), in.size());
        ASSERT_TRUE(plain) << "size " << in.size();
        EXPECT_EQ(*plain, in) << "size " << in.size();
    }
}

TEST(PakCompression, RefusesGarbage)
{
    std::string text(500, 'q');
    std::string packed = Pak::Compress(text);
    EXPECT_FALSE(Pak::Decompress(packed.data(), packed.size(), text.size() + 1));
    EXPECT_FALSE(Pak::Decompress(packed.data(), packed.size(), text.size() - 1));
    EXPECT_FALSE(Pak::Decompress(packed.data(), packed.size() - 1, text.size()));
    const char bad[] = { char(0x0F), char(0x10), char(0x00) };
    EXPECT_FALSE(Pak::Decompress(bad, sizeof(bad), 100));
}

TEST(PakCompression, PakIsSmallerAndStaysReadable)
{
    TempDir dir;
    std::string text;
    for (int i = 0; i < 2000; ++i) text += "line " + std::to_string(i % 10) + " of a repetitive world file\n";
    std::string noise;
    unsigned s = 99;
    for (int i = 0; i < 5000; ++i) { s = s * 1664525u + 1013904223u; noise.push_back(char(s >> 24)); }

    Pak::PakWriter packed, stored;
    stored.SetCompression(false);
    for (Pak::PakWriter* w : { &packed, &stored }) { w->Add("a.world", text); w->Add("b.png", noise); }
    ASSERT_TRUE(packed.Write(dir.Sub("p.shardpak")));
    ASSERT_TRUE(stored.Write(dir.Sub("s.shardpak")));
    EXPECT_LT(dir.Read("p.shardpak").size(), dir.Read("s.shardpak").size() / 2);

    Pak::PakReader reader;
    ASSERT_TRUE(reader.Open(dir.Sub("p.shardpak")));
    EXPECT_TRUE(reader.Find("a.world")->compressed);
    EXPECT_FALSE(reader.Find("b.png")->compressed); // incompressible data stays raw
    EXPECT_EQ(*reader.Read("a.world"), text);
    EXPECT_EQ(*reader.Read("b.png"), noise);
    EXPECT_TRUE(reader.Verify().empty());
}

TEST(PakCompression, UnpackCachedReusesTheFolder)
{
    TempDir dir;
    dir.Write("src/a/b.txt", std::string(1000, 'b'));
    dir.Write("src/c.txt", "c");
    Pak::PakWriter writer;
    ASSERT_EQ(writer.AddFolder(dir.Sub("src")), 2u);
    ASSERT_TRUE(writer.Write(dir.Sub("res.shardpak")));

    const std::string a = Pak::UnpackCached(dir.Sub("res.shardpak"), dir.Sub("cache"));
    ASSERT_FALSE(a.empty());
    EXPECT_EQ(dir.Read("cache/" + a.substr(a.rfind('/') + 1) + "/a/b.txt"), std::string(1000, 'b'));
    EXPECT_EQ(Pak::UnpackCached(dir.Sub("res.shardpak"), dir.Sub("cache")), a);

    // A rebuilt pak gets its own folder
    writer.Add("c.txt", "changed");
    ASSERT_TRUE(writer.Write(dir.Sub("res.shardpak")));
    EXPECT_NE(Pak::UnpackCached(dir.Sub("res.shardpak"), dir.Sub("cache")), a);
}

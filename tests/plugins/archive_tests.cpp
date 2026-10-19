#include <gtest/gtest.h>

#include "engine/plugins/archive.hpp"
#include "engine/plugins/crypto.hpp"
#include "plugins/plugin_test_utils.hpp"

using namespace Shard::Engine::Plugins;
using PluginTest::TempDir;

namespace {
    // Made by Python's zipfile : "big.txt" (deflated, 4900 bytes), "sub/small.txt" (deflated), "stored.bin" (0..255, stored)
    const uint8_t kSampleZip[] = { 80,75,3,4,20,0,0,0,8,0,0,0,33,0,224,2,176,0,108,1,0,0,36,19,0,0,7,0,0,0,98,105,103,46,116,120,116,237,208,215,141,101,49,12,3,208,86,94,5,211,147,156,115,144,28,85,253,222,62,214,127,196,129,0,130,34,7,168,126,45,77,235,203,15,80,58,191,244,223,143,158,62,125,250,244,233,211,167,79,159,62,125,250,244,233,211,167,79,255,91,213,196,87,122,213,110,109,51,179,85,13,114,25,247,194,174,254,176,35,21,1,96,34,228,101,203,129,238,190,51,116,201,173,47,4,40,56,149,153,39,168,19,207,238,165,47,27,2,181,158,73,52,119,56,151,101,18,238,187,78,146,117,117,117,77,207,169,29,104,34,236,65,148,167,49,221,193,181,136,46,247,68,169,250,133,227,64,230,211,89,247,139,182,136,150,8,109,47,45,149,4,136,131,71,172,3,216,77,131,100,36,35,123,177,164,132,10,247,122,231,21,15,147,130,52,198,119,179,252,220,161,238,216,154,130,144,99,97,235,149,63,221,142,2,14,178,22,199,212,221,87,65,55,121,247,234,250,252,6,18,199,53,139,56,65,91,17,164,12,225,152,66,94,3,10,178,84,205,221,163,139,108,147,178,180,10,217,166,86,14,189,65,28,57,128,177,145,137,117,44,214,47,149,49,45,108,23,157,60,66,106,99,208,250,27,71,247,41,70,21,220,184,187,105,194,171,162,40,50,179,214,81,13,226,44,9,29,73,159,2,161,170,223,20,17,96,44,144,229,75,214,49,21,163,170,89,206,28,85,50,99,64,191,91,84,118,70,1,192,225,124,111,201,49,75,25,71,85,222,242,184,184,219,74,254,107,14,214,37,233,229,173,114,210,140,46,7,17,77,228,127,80,75,3,4,20,0,0,0,8,0,0,0,33,0,172,42,147,216,4,0,0,0,2,0,0,0,13,0,0,0,115,117,98,47,115,109,97,108,108,46,116,120,116,203,200,4,0,80,75,3,4,20,0,0,0,0,0,0,0,33,0,115,140,5,41,0,1,0,0,0,1,0,0,10,0,0,0,115,116,111,114,101,100,46,98,105,110,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255,80,75,1,2,20,3,20,0,0,0,8,0,0,0,33,0,224,2,176,0,108,1,0,0,36,19,0,0,7,0,0,0,0,0,0,0,0,0,0,0,128,1,0,0,0,0,98,105,103,46,116,120,116,80,75,1,2,20,3,20,0,0,0,8,0,0,0,33,0,172,42,147,216,4,0,0,0,2,0,0,0,13,0,0,0,0,0,0,0,0,0,0,0,128,1,145,1,0,0,115,117,98,47,115,109,97,108,108,46,116,120,116,80,75,1,2,20,3,20,0,0,0,0,0,0,0,33,0,115,140,5,41,0,1,0,0,0,1,0,0,10,0,0,0,0,0,0,0,0,0,0,0,128,1,192,1,0,0,115,116,111,114,101,100,46,98,105,110,80,75,5,6,0,0,0,0,3,0,3,0,168,0,0,0,232,2,0,0,0,0 };
    // One entry named "../evil.txt"
    const uint8_t kEvilZip[] = { 80,75,3,4,20,0,0,0,0,0,0,0,33,0,131,22,220,140,1,0,0,0,1,0,0,0,11,0,0,0,46,46,47,101,118,105,108,46,116,120,116,120,80,75,1,2,20,3,20,0,0,0,0,0,0,0,33,0,131,22,220,140,1,0,0,0,1,0,0,0,11,0,0,0,0,0,0,0,0,0,0,0,128,1,0,0,0,0,46,46,47,101,118,105,108,46,116,120,116,80,75,5,6,0,0,0,0,1,0,1,0,57,0,0,0,42,0,0,0,0,0 };

    std::vector<uint8_t> Bytes(const uint8_t* data, size_t size) { return std::vector<uint8_t>(data, data + size); }
}

TEST(Crc32, KnownValues)
{
    EXPECT_EQ(Crc32("123456789", 9), 0xCBF43926u);
    EXPECT_EQ(Crc32("", 0), 0u);
    EXPECT_EQ(Crc32("56789", 5, Crc32("1234", 4)), 0xCBF43926u);          // streaming
}

TEST(Archive, ReadsADeflatedArchiveMadeByAnotherTool)
{
    auto zip = ZipArchive::OpenMemory(Bytes(kSampleZip, sizeof(kSampleZip)));
    ASSERT_TRUE(zip) << zip.GetError().message;
    ASSERT_EQ(zip.Value().Entries().size(), 3u);

    auto big = zip.Value().Read("big.txt");
    ASSERT_TRUE(big) << big.GetError().message;
    EXPECT_EQ(big.Value().size(), 4900u);
    EXPECT_EQ(Crypto::ToHex(Crypto::Sha256Of(big.Value().data(), big.Value().size())), "266c980c95117b4244c0e55f70348e4d17fc5ef2f227485ca6ee985ce9b7b41b");

    auto small = zip.Value().Read("sub/small.txt");
    ASSERT_TRUE(small);
    EXPECT_EQ(std::string(small.Value().begin(), small.Value().end()), "hi");

    auto stored = zip.Value().Read("stored.bin");
    ASSERT_TRUE(stored);
    ASSERT_EQ(stored.Value().size(), 256u);
    for (size_t i = 0; i < 256; ++i) EXPECT_EQ(stored.Value()[i], uint8_t(i));
    EXPECT_FALSE(zip.Value().Read("absent"));
}

TEST(Archive, ExtractsToAFolder)
{
    TempDir dir;
    auto zip = ZipArchive::OpenMemory(Bytes(kSampleZip, sizeof(kSampleZip)));
    ASSERT_TRUE(zip);
    auto n = zip.Value().ExtractAll(dir.Sub("out"));
    ASSERT_TRUE(n) << n.GetError().message;
    EXPECT_EQ(n.Value(), 3u);
    EXPECT_EQ(dir.Read("out/sub/small.txt"), "hi");
    EXPECT_EQ(dir.Read("out/big.txt").size(), 4900u);
}

TEST(Archive, RefusesAnEntryThatLeavesTheFolder)
{
    auto zip = ZipArchive::OpenMemory(Bytes(kEvilZip, sizeof(kEvilZip)));
    ASSERT_FALSE(zip);
    EXPECT_NE(zip.GetError().message.find("leave"), std::string::npos);
}

TEST(Archive, PathChecks)
{
    EXPECT_TRUE(IsSafeArchivePath("a/b/c.txt"));
    EXPECT_TRUE(IsSafeArchivePath("content/tables/t.json"));
    for (const char* bad : { "", "/etc/passwd", "../x", "a/../../x", "a/./b", "C:/x", "a\\b", "a/..", "..", "." })
        EXPECT_FALSE(IsSafeArchivePath(bad)) << bad;
}

TEST(Archive, DetectsACorruptedEntry)
{
    std::vector<uint8_t> bytes = Bytes(kSampleZip, sizeof(kSampleZip));
    // flip a byte inside the stored entry's data, near the middle of the file's end part
    auto zip = ZipArchive::OpenMemory(bytes);
    ASSERT_TRUE(zip);
    const auto* stored = zip.Value().Find("stored.bin");
    ASSERT_NE(stored, nullptr);
    bytes[stored->localHeaderOffset + 30 + 10 + 20] ^= 0x55;          // 10 : the name, 20 : into the data
    auto corrupted = ZipArchive::OpenMemory(bytes);
    ASSERT_TRUE(corrupted);
    auto read = corrupted.Value().Read("stored.bin");
    ASSERT_FALSE(read);
    EXPECT_NE(read.GetError().message.find("checksum"), std::string::npos);
}

TEST(Archive, RefusesGarbageAndTruncatedFiles)
{
    EXPECT_FALSE(ZipArchive::OpenMemory({}));
    EXPECT_FALSE(ZipArchive::OpenMemory(std::vector<uint8_t>(100, 0x42)));
    std::vector<uint8_t> bytes = Bytes(kSampleZip, sizeof(kSampleZip));
    bytes.resize(bytes.size() - 30);
    EXPECT_FALSE(ZipArchive::OpenMemory(bytes));
}

TEST(Archive, ARatioAboveTheLimitIsAZipBomb)
{
    ArchiveLimits tight;
    tight.maxCompressionRatio = 2;
    auto zip = ZipArchive::OpenMemory(Bytes(kSampleZip, sizeof(kSampleZip)), tight);
    ASSERT_FALSE(zip);
    EXPECT_NE(zip.GetError().message.find("expands"), std::string::npos);

    ArchiveLimits small;
    small.maxTotalSize = 1000;
    EXPECT_FALSE(ZipArchive::OpenMemory(Bytes(kSampleZip, sizeof(kSampleZip)), small));
    ArchiveLimits few;
    few.maxEntries = 2;
    EXPECT_FALSE(ZipArchive::OpenMemory(Bytes(kSampleZip, sizeof(kSampleZip)), few));
}

TEST(Archive, InflateStopsAtItsLimit)
{
    auto zip = ZipArchive::OpenMemory(Bytes(kSampleZip, sizeof(kSampleZip)));
    ASSERT_TRUE(zip);
    const auto* big = zip.Value().Find("big.txt");
    ASSERT_NE(big, nullptr);
    const size_t dataAt = big->localHeaderOffset + 30 + 7;
    auto limited = Inflate(Bytes(kSampleZip, sizeof(kSampleZip)).data() + dataAt, size_t(big->compressedSize), 100);
    EXPECT_FALSE(limited);
    auto garbage = std::vector<uint8_t>{ 0xff, 0xff, 0xff, 0xff };
    EXPECT_FALSE(Inflate(garbage.data(), garbage.size(), 1000));
}

TEST(Archive, WriterAndReaderRoundTripADirectory)
{
    TempDir dir;
    dir.Write("src/shard-plugin.json", "{ \"a\": 1 }");
    dir.Write("src/content/tables/t.json", "{}");
    dir.Write("src/content/big.bin", std::string(100000, 'x'));
    dir.Write("src/skip.txt", "skipped");
    ZipWriter writer;
    auto added = writer.AddDirectory(dir.Sub("src"), "", { "skip.txt" });
    ASSERT_TRUE(added);
    EXPECT_EQ(added.Value(), 3u);
    ASSERT_TRUE(writer.Save(dir.Sub("pack.shardplugin")));

    auto zip = ZipArchive::Open(dir.Sub("pack.shardplugin"));
    ASSERT_TRUE(zip) << zip.GetError().message;
    EXPECT_EQ(zip.Value().Entries().size(), 3u);
    EXPECT_FALSE(zip.Value().Find("skip.txt"));
    auto json = zip.Value().Read("shard-plugin.json");
    ASSERT_TRUE(json);
    EXPECT_EQ(std::string(json.Value().begin(), json.Value().end()), "{ \"a\": 1 }");
    ASSERT_TRUE(zip.Value().ExtractAll(dir.Sub("out")));
    EXPECT_EQ(dir.Read("out/content/big.bin").size(), 100000u);

    // the same folder gives the same bytes
    ZipWriter again;
    again.AddDirectory(dir.Sub("src"), "", { "skip.txt" });
    EXPECT_EQ(writer.Finish(), again.Finish());
}

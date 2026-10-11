#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/config/cvar.hpp"
#include "engine/renderer/rhi/resources/texture/image_export.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Rendering;

namespace {
    namespace fs = std::filesystem;

    // A scratch folder with a texture cache in it, removed with the fixture
    class TextureDecodeTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            m_Dir = fs::temp_directory_path() / ("shard_texture_decode_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
            std::error_code ec;
            fs::remove_all(m_Dir, ec);
            fs::create_directories(m_Dir);
            TextureCache::SetDirectory((m_Dir / "cache").generic_string());
            Core::CVarRegistry::Global().Execute("texture.compress true");
        }

        void TearDown() override
        {
            TextureCache::SetDirectory("");
            Core::CVarRegistry::Global().Execute("texture.compress true");
            std::error_code ec;
            fs::remove_all(m_Dir, ec);
        }

        // Writes a PNG of `channels` channels where each pixel is f(x, y, channel)
        template <class F>
        Filesystem::Path WritePng(const char* name, uint32_t width, uint32_t height, uint32_t channels, F f)
        {
            std::vector<uint8_t> pixels(size_t(width) * height * channels);
            for (uint32_t y = 0; y < height; y++)
                for (uint32_t x = 0; x < width; x++)
                    for (uint32_t c = 0; c < channels; c++)
                        pixels[(size_t(y) * width + x) * channels + c] = f(x, y, c);
            const Filesystem::Path path((m_Dir / name).generic_string());
            EXPECT_TRUE(ImageExport::WritePNG(path, width, height, channels, pixels.data()));
            return path;
        }

        size_t CachedFiles() const
        {
            const fs::path cache = m_Dir / "cache";
            if (!fs::exists(cache))
                return 0;
            size_t count = 0;
            for (const auto& entry : fs::directory_iterator(cache))
                if (entry.path().extension() == ".stx")
                    count++;
            return count;
        }

        fs::path m_Dir;
    };

    uint8_t Gradient(uint32_t x, uint32_t y, uint32_t c)
    {
        return uint8_t((x * 3 + y * 2 + c * 40) & 0xFF);
    }
}

TEST_F(TextureDecodeTest, ColourImageComesBackAsBC7WithItsMips) {
    const Filesystem::Path path = WritePng("albedo.png", 256, 256, 3, Gradient);
    const TextureDecodeResult result = DecodeTextureFileForUpload(path);

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.compressed);
    EXPECT_TRUE(result.pixels.empty());                    // only the blocks go on
    EXPECT_EQ(result.width, 256);
    EXPECT_EQ(result.height, 256);
    EXPECT_EQ(result.compressed->format, BlockFormat::BC7);
    EXPECT_EQ(result.compressed->mips.size(), MipCount(256, 256));
    EXPECT_EQ(CachedFiles(), 1u);
}

TEST_F(TextureDecodeTest, TheSecondDecodeComesFromTheCache) {
    const Filesystem::Path path = WritePng("albedo.png", 256, 256, 4, Gradient);

    const TextureDecodeResult first = DecodeTextureFileForUpload(path);
    ASSERT_TRUE(first.compressed);
    ASSERT_EQ(CachedFiles(), 1u);

    const TextureDecodeResult second = DecodeTextureFileForUpload(path);
    ASSERT_TRUE(second.compressed);
    EXPECT_EQ(second.compressed->mips, first.compressed->mips);
    EXPECT_EQ(second.width, first.width);
    EXPECT_EQ(CachedFiles(), 1u);                          // nothing new was written
}

TEST_F(TextureDecodeTest, EditingTheSourceMakesANewCacheEntry) {
    const Filesystem::Path path = WritePng("albedo.png", 256, 256, 3, Gradient);
    ASSERT_TRUE(DecodeTextureFileForUpload(path).compressed);
    ASSERT_EQ(CachedFiles(), 1u);

    // Another image under the same name : its size and date are not those of the cached one
    WritePng("albedo.png", 256, 256, 3, [](uint32_t x, uint32_t y, uint32_t c) { return uint8_t(255 - Gradient(x, y, c)); });
    const TextureDecodeResult edited = DecodeTextureFileForUpload(path);
    ASSERT_TRUE(edited.compressed);
    EXPECT_EQ(CachedFiles(), 2u);
}

TEST_F(TextureDecodeTest, NormalMapsKeepTwoChannels) {
    const Filesystem::Path path = WritePng("normal.png", 256, 256, 3, [](uint32_t, uint32_t, uint32_t c) { return uint8_t(c == 2 ? 255 : 128); });
    const TextureDecodeResult result = DecodeTextureFileForUpload(path);
    ASSERT_TRUE(result.compressed);
    EXPECT_EQ(result.compressed->format, BlockFormat::BC5);
}

TEST_F(TextureDecodeTest, SingleChannelImagesAreBC4) {
    const Filesystem::Path path = WritePng("roughness.png", 256, 256, 1, [](uint32_t x, uint32_t y, uint32_t) { return uint8_t((x + y) & 0xFF); });
    const TextureDecodeResult result = DecodeTextureFileForUpload(path);
    ASSERT_TRUE(result.compressed);
    EXPECT_EQ(result.compressed->format, BlockFormat::BC4);
    EXPECT_EQ(result.compressed->mips[0].size(), MipBytes(BlockFormat::BC4, 256, 256));
}

TEST_F(TextureDecodeTest, SmallImagesStayUncompressed) {
    const Filesystem::Path path = WritePng("icon.png", 64, 64, 4, Gradient);
    const TextureDecodeResult result = DecodeTextureFileForUpload(path);
    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.compressed);
    EXPECT_EQ(result.pixels.size(), size_t(64) * 64 * 4);
    EXPECT_EQ(CachedFiles(), 0u);
}

TEST_F(TextureDecodeTest, TurningCompressionOffGivesThePixels) {
    const Filesystem::Path path = WritePng("albedo.png", 256, 256, 3, Gradient);
    ASSERT_TRUE(Core::CVarRegistry::Global().Execute("texture.compress false").ok);

    const TextureDecodeResult result = DecodeTextureFileForUpload(path);
    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.compressed);
    EXPECT_EQ(result.pixels.size(), size_t(256) * 256 * 3);
    EXPECT_EQ(result.format, TextureInternalFormat::RGB);
    EXPECT_EQ(CachedFiles(), 0u);
}

TEST_F(TextureDecodeTest, NoCacheDirectoryStillCompresses) {
    TextureCache::SetDirectory("");
    const Filesystem::Path path = WritePng("albedo.png", 256, 256, 3, Gradient);
    const TextureDecodeResult result = DecodeTextureFileForUpload(path);
    ASSERT_TRUE(result.compressed);
    EXPECT_EQ(CachedFiles(), 0u);
}

TEST_F(TextureDecodeTest, ADamagedCacheFileIsEncodedAgain) {
    const Filesystem::Path path = WritePng("albedo.png", 256, 256, 3, Gradient);
    const TextureDecodeResult first = DecodeTextureFileForUpload(path);
    ASSERT_TRUE(first.compressed);

    // Truncate what the cache holds
    for (const auto& entry : fs::directory_iterator(m_Dir / "cache"))
        if (entry.path().extension() == ".stx")
            fs::resize_file(entry.path(), 40);

    const TextureDecodeResult second = DecodeTextureFileForUpload(path);
    ASSERT_TRUE(second.compressed);
    EXPECT_EQ(second.compressed->mips, first.compressed->mips);     // same image, encoded again (and stored again)
}

TEST_F(TextureDecodeTest, ADecodeResultMakesNoTextureWithoutAnApi) {
    // Texture2D::Create needs a graphics API : a failed decode is what must be refused cleanly, before it gets there
    TextureDecodeResult failed;
    TextureSpecifications specs;
    EXPECT_EQ(Texture2D::Create(specs, failed), nullptr);
}

TEST_F(TextureDecodeTest, MissingFileFails) {
    const TextureDecodeResult result = DecodeTextureFileForUpload(Filesystem::Path((m_Dir / "nope.png").generic_string()));
    EXPECT_FALSE(result.success);
}

TEST_F(TextureDecodeTest, MaxSizeLeavesOutTheBiggestLevels) {
    // No limit by default
    EXPECT_EQ(MipsToSkip(4096, 4096), 0u);

    ASSERT_TRUE(Core::CVarRegistry::Global().Execute("texture.max_size 2048").ok);
    EXPECT_EQ(MipsToSkip(4096, 4096), 1u);
    EXPECT_EQ(MipsToSkip(8192, 2048), 2u);      // by the biggest side
    EXPECT_EQ(MipsToSkip(2048, 2048), 0u);
    EXPECT_EQ(MipsToSkip(1024, 512), 0u);       // never enlarged

    ASSERT_TRUE(Core::CVarRegistry::Global().Execute("texture.max_size 1").ok);
    EXPECT_LE(MipsToSkip(4096, 4096), 10u);     // stops while there is still a texture left

    ASSERT_TRUE(Core::CVarRegistry::Global().Execute("texture.max_size 0").ok);
    EXPECT_EQ(MipsToSkip(4096, 4096), 0u);
}

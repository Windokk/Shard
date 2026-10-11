#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <vector>

#include "engine/renderer/rhi/resources/texture/texture_compression.hpp"

using namespace Shard::Engine::Rendering;

namespace {
    double Psnr(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
    {
        double sum = 0.0;
        for (size_t i = 0; i < a.size(); i++)
        {
            const double d = double(a[i]) - double(b[i]);
            sum += d * d;
        }
        sum /= double(a.size());
        return sum == 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / sum);
    }

    // Colours that sit on one line of colour space (what a mode 6 block describes exactly, up to its precision) :
    // a ramp over the 4x4 pixels. `alphaRamp` makes the alpha follow the same ramp.
    std::vector<uint8_t> LineBlock(bool alphaRamp)
    {
        std::vector<uint8_t> px(64);
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
            {
                const float t = float(x + y) / 6.0f;
                uint8_t* p = &px[(y * 4 + x) * 4];
                p[0] = uint8_t(20 + 200 * t);
                p[1] = uint8_t(210 - 150 * t);
                p[2] = uint8_t(60 + 40 * t);
                p[3] = alphaRamp ? uint8_t(30 + 200 * t) : 255;
            }
        return px;
    }

    // Colours spread over a plane : more than one line can hold, so some error is expected
    std::vector<uint8_t> PlaneBlock()
    {
        std::vector<uint8_t> px(64);
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
            {
                uint8_t* p = &px[(y * 4 + x) * 4];
                p[0] = uint8_t(20 + x * 30 + y * 10);
                p[1] = uint8_t(200 - x * 25 - y * 12);
                p[2] = uint8_t(60 + x * 12);
                p[3] = 255;
            }
        return px;
    }

    // Level 0 of a BC7 image, decoded back to RGBA
    std::vector<uint8_t> DecodeLevel0RGBA(const CompressedImage& image)
    {
        const uint32_t bx = BlocksAcross(image.width), by = BlocksAcross(image.height);
        std::vector<uint8_t> out(size_t(image.width) * image.height * 4);
        for (uint32_t j = 0; j < by; j++)
            for (uint32_t i = 0; i < bx; i++)
            {
                uint8_t block[64];
                EXPECT_TRUE(DecodeBC7Block(&image.mips[0][(size_t(j) * bx + i) * 16], block));
                for (uint32_t y = 0; y < 4; y++)
                    for (uint32_t x = 0; x < 4; x++)
                    {
                        const uint32_t px = i * 4 + x, py = j * 4 + y;
                        if (px >= image.width || py >= image.height)
                            continue;
                        for (int c = 0; c < 4; c++)
                            out[(size_t(py) * image.width + px) * 4 + c] = block[(y * 4 + x) * 4 + c];
                    }
            }
        return out;
    }

    std::vector<uint8_t> FlatNormalMap(uint32_t w, uint32_t h)
    {
        std::vector<uint8_t> flat(size_t(w) * h * 3);
        for (size_t i = 0; i < flat.size(); i += 3)
        {
            flat[i] = 128;
            flat[i + 1] = 128;
            flat[i + 2] = 255;
        }
        return flat;
    }
}

TEST(TextureCompression, MipChainSizes) {
    EXPECT_EQ(MipCount(1, 1), 1u);
    EXPECT_EQ(MipCount(4096, 4096), 13u);
    EXPECT_EQ(MipCount(1000, 600), 10u);
    EXPECT_EQ(MipBytes(BlockFormat::BC7, 4096, 4096), size_t(4096) * 4096);     // one byte per texel
    EXPECT_EQ(MipBytes(BlockFormat::BC4, 4096, 4096), size_t(4096) * 4096 / 2); // half a byte
    EXPECT_EQ(MipBytes(BlockFormat::BC7, 1, 1), 16u);                           // a block is the smallest unit
    EXPECT_EQ(MipBytes(BlockFormat::BC5, 130, 70), size_t(33) * 18 * 16);
}

TEST(TextureCompression, SmallImagesAreLeftAlone) {
    EXPECT_FALSE(ShouldCompress(64, 64));
    EXPECT_FALSE(ShouldCompress(1024, 64));
    EXPECT_TRUE(ShouldCompress(128, 128));
    EXPECT_TRUE(ShouldCompress(4096, 1024));
}

TEST(TextureCompression, BC7FlatBlockIsExact) {
    std::vector<uint8_t> px(64);
    for (int i = 0; i < 16; i++)
    {
        px[i * 4 + 0] = 10;
        px[i * 4 + 1] = 200;
        px[i * 4 + 2] = 77;
        px[i * 4 + 3] = 255;
    }
    uint8_t block[16], decoded[64];
    EncodeBC7Block(px.data(), block);
    ASSERT_TRUE(DecodeBC7Block(block, decoded));
    for (int i = 0; i < 64; i++)
        EXPECT_NEAR(decoded[i], px[i], 1) << "byte " << i;
}

TEST(TextureCompression, BC7LineBlockIsClose) {
    const std::vector<uint8_t> px = LineBlock(false);
    uint8_t block[16];
    std::vector<uint8_t> decoded(64);
    EncodeBC7Block(px.data(), block);
    ASSERT_TRUE(DecodeBC7Block(block, decoded.data()));
    EXPECT_GT(Psnr(px, decoded), 40.0);
}

TEST(TextureCompression, BC7PlaneBlockIsReasonable) {
    const std::vector<uint8_t> px = PlaneBlock();
    uint8_t block[16];
    std::vector<uint8_t> decoded(64);
    EncodeBC7Block(px.data(), block);
    ASSERT_TRUE(DecodeBC7Block(block, decoded.data()));
    EXPECT_GT(Psnr(px, decoded), 34.0);
}

TEST(TextureCompression, BC7KeepsAConstantAlphaExact) {
    std::vector<uint8_t> px = PlaneBlock();
    for (int i = 0; i < 16; i++)
        px[i * 4 + 3] = 129;
    uint8_t block[16], decoded[64];
    EncodeBC7Block(px.data(), block);
    ASSERT_TRUE(DecodeBC7Block(block, decoded));
    for (int i = 0; i < 16; i++)
        EXPECT_EQ(decoded[i * 4 + 3], 129);
}

TEST(TextureCompression, BC7HandlesAVaryingAlpha) {
    const std::vector<uint8_t> px = LineBlock(true);        // a soft cutout : alpha ramps along with the colour
    uint8_t block[16];
    std::vector<uint8_t> decoded(64);
    EncodeBC7Block(px.data(), block);
    ASSERT_TRUE(DecodeBC7Block(block, decoded.data()));
    EXPECT_GT(Psnr(px, decoded), 40.0);
}

TEST(TextureCompression, BC7SurvivesNoise) {
    std::mt19937 rng(42);
    for (int n = 0; n < 200; n++)
    {
        std::vector<uint8_t> px(64);
        for (uint8_t& v : px)
            v = uint8_t(rng());
        uint8_t block[16], decoded[64];
        EncodeBC7Block(px.data(), block);
        EXPECT_TRUE(DecodeBC7Block(block, decoded));        // always a valid mode 6 block, whatever the input
    }
}

TEST(TextureCompression, BC7ModeBitsAreMode6) {
    std::mt19937 rng(7);
    for (int n = 0; n < 200; n++)
    {
        std::vector<uint8_t> px(64);
        for (uint8_t& v : px)
            v = uint8_t(rng());
        uint8_t block[16];
        EncodeBC7Block(px.data(), block);
        EXPECT_EQ(block[0] & 0x7F, 1 << 6);                 // mode 6 : six zeros, then a one
    }
}

TEST(TextureCompression, CompressImageBuildsEveryMipForEachChannelCount) {
    struct Case { uint32_t channels; BlockFormat format; };
    for (const Case c : { Case{1, BlockFormat::BC4}, Case{2, BlockFormat::BC5}, Case{3, BlockFormat::BC7}, Case{4, BlockFormat::BC7} })
    {
        const uint32_t w = 256, h = 128;
        std::vector<uint8_t> px(size_t(w) * h * c.channels, 90);
        CompressedImage image;
        ASSERT_TRUE(CompressImage(px.data(), w, h, c.channels, image)) << c.channels;
        EXPECT_EQ(image.format, c.format);
        ASSERT_EQ(image.mips.size(), MipCount(w, h));
        uint32_t mw = w, mh = h;
        for (const auto& mip : image.mips)
        {
            EXPECT_EQ(mip.size(), MipBytes(c.format, mw, mh));
            mw = std::max(1u, mw / 2);
            mh = std::max(1u, mh / 2);
        }
    }
}

TEST(TextureCompression, OddSizesAreEdgeClamped) {
    const uint32_t w = 130, h = 70;
    std::vector<uint8_t> px(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
        {
            uint8_t* p = &px[(size_t(y) * w + x) * 4];
            p[0] = uint8_t(x * 255 / (w - 1));
            p[1] = uint8_t(y * 255 / (h - 1));
            p[2] = 80;
            p[3] = 255;
        }
    CompressedImage image;
    ASSERT_TRUE(CompressImage(px.data(), w, h, 4, image));
    EXPECT_EQ(image.mips.size(), MipCount(w, h));
    EXPECT_EQ(image.mips[0].size(), MipBytes(BlockFormat::BC7, w, h));

    // The border blocks are filled from the edge pixels : the whole level still matches the source
    EXPECT_GT(Psnr(px, DecodeLevel0RGBA(image)), 40.0);
}

TEST(TextureCompression, SmoothImageKeepsItsQuality) {
    const uint32_t w = 256, h = 256;
    std::vector<uint8_t> px(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
        {
            uint8_t* p = &px[(size_t(y) * w + x) * 4];
            p[0] = uint8_t(128 + 100 * std::sin(x * 0.05));
            p[1] = uint8_t(128 + 100 * std::sin(y * 0.04 + 1.0));
            p[2] = uint8_t((x + y) / 2);
            p[3] = 255;
        }
    CompressedImage image;
    ASSERT_TRUE(CompressImage(px.data(), w, h, 4, image));
    EXPECT_GT(Psnr(px, DecodeLevel0RGBA(image)), 44.0);
}

TEST(TextureCompression, MultiThreadedEncodingGivesTheSameBlocks) {
    const uint32_t w = 512, h = 512;
    std::mt19937 rng(3);
    std::vector<uint8_t> px(size_t(w) * h * 4);
    for (uint8_t& v : px)
        v = uint8_t(rng() & 0xF0);
    CompressedImage one, many;
    ASSERT_TRUE(CompressImage(px.data(), w, h, 4, one, false, 1));
    ASSERT_TRUE(CompressImage(px.data(), w, h, 4, many, false, 4));
    ASSERT_EQ(one.mips.size(), many.mips.size());
    for (size_t i = 0; i < one.mips.size(); i++)
        EXPECT_EQ(one.mips[i], many.mips[i]) << "mip " << i;
}

TEST(TextureCompression, NormalMapDetection) {
    const uint32_t w = 256, h = 256;
    std::vector<uint8_t> normals(size_t(w) * h * 3);
    std::vector<uint8_t> albedo(size_t(w) * h * 3);
    std::mt19937 rng(11);
    for (uint32_t i = 0; i < w * h; i++)
    {
        // A tilted unit vector pointing out of the surface
        const double tx = (double(rng() % 1000) / 1000.0 - 0.5) * 0.6, ty = (double(rng() % 1000) / 1000.0 - 0.5) * 0.6;
        const double tz = std::sqrt(1.0 - tx * tx - ty * ty);
        normals[i * 3 + 0] = uint8_t(std::lround((tx * 0.5 + 0.5) * 255.0));
        normals[i * 3 + 1] = uint8_t(std::lround((ty * 0.5 + 0.5) * 255.0));
        normals[i * 3 + 2] = uint8_t(std::lround((tz * 0.5 + 0.5) * 255.0));
        albedo[i * 3 + 0] = uint8_t(60 + (rng() % 120));
        albedo[i * 3 + 1] = uint8_t(40 + (rng() % 160));
        albedo[i * 3 + 2] = uint8_t(rng() % 90);
    }
    EXPECT_TRUE(LooksLikeNormalMap(normals.data(), w, h, 3));
    EXPECT_FALSE(LooksLikeNormalMap(albedo.data(), w, h, 3));
    EXPECT_FALSE(LooksLikeNormalMap(normals.data(), w, h, 1));              // one channel : never a normal map

    // The flat "no detail" normal map is one too
    const std::vector<uint8_t> flat = FlatNormalMap(w, h);
    EXPECT_TRUE(LooksLikeNormalMap(flat.data(), w, h, 3));
}

TEST(TextureCompression, NormalMapsAreEncodedAsTwoChannels) {
    const uint32_t w = 256, h = 256;
    const std::vector<uint8_t> flat = FlatNormalMap(w, h);
    CompressedImage image;
    ASSERT_TRUE(CompressImage(flat.data(), w, h, 3, image, true));
    EXPECT_EQ(image.format, BlockFormat::BC5);
    EXPECT_EQ(image.mips[0].size(), MipBytes(BlockFormat::BC5, w, h));
    EXPECT_FALSE(CompressImage(flat.data(), w, h, 1, image, true));          // needs the vector's channels
}

TEST(TextureCompression, RejectsBadInput) {
    CompressedImage image;
    std::vector<uint8_t> px(16 * 16 * 4);
    EXPECT_FALSE(CompressImage(nullptr, 16, 16, 4, image));
    EXPECT_FALSE(CompressImage(px.data(), 0, 16, 4, image));
    EXPECT_FALSE(CompressImage(px.data(), 16, 16, 5, image));
    EXPECT_FALSE(CompressImage(px.data(), 16, 16, 0, image));
}

TEST(TextureCache, SerializationRoundTrips) {
    const uint32_t w = 128, h = 128;
    std::mt19937 rng(5);
    std::vector<uint8_t> px(size_t(w) * h * 4);
    for (uint8_t& v : px)
        v = uint8_t(rng());
    CompressedImage original;
    ASSERT_TRUE(CompressImage(px.data(), w, h, 4, original));

    const std::vector<uint8_t> bytes = TextureCache::Serialize(original);
    CompressedImage loaded;
    ASSERT_TRUE(TextureCache::Deserialize(bytes.data(), bytes.size(), loaded));
    EXPECT_EQ(loaded.width, original.width);
    EXPECT_EQ(loaded.height, original.height);
    EXPECT_EQ(loaded.format, original.format);
    EXPECT_EQ(loaded.mips, original.mips);
}

TEST(TextureCache, RejectsDamagedFiles) {
    const uint32_t w = 128, h = 128;
    std::vector<uint8_t> px(size_t(w) * h * 4, 100);
    CompressedImage original;
    ASSERT_TRUE(CompressImage(px.data(), w, h, 4, original));
    const std::vector<uint8_t> good = TextureCache::Serialize(original);

    CompressedImage out;
    // Truncated anywhere
    for (size_t cut : { size_t(0), size_t(3), size_t(20), good.size() / 2, good.size() - 1 })
        EXPECT_FALSE(TextureCache::Deserialize(good.data(), cut, out)) << "cut at " << cut;

    // Wrong magic
    std::vector<uint8_t> bad = good;
    bad[0] = 'X';
    EXPECT_FALSE(TextureCache::Deserialize(bad.data(), bad.size(), out));

    // Another version of the format
    bad = good;
    bad[4] = uint8_t(TextureCache::kVersion + 1);
    EXPECT_FALSE(TextureCache::Deserialize(bad.data(), bad.size(), out));

    // A mip count that does not fit the size
    bad = good;
    bad[20] = 3;
    EXPECT_FALSE(TextureCache::Deserialize(bad.data(), bad.size(), out));

    // An absurd size
    bad = good;
    bad[8] = bad[9] = bad[10] = bad[11] = 0;
    EXPECT_FALSE(TextureCache::Deserialize(bad.data(), bad.size(), out));
}

TEST(TextureCache, StoresAndLoadsThroughTheDisk) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "shard_texture_cache_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    TextureCache::SetDirectory((dir / "textures").generic_string());

    // A source file the key can be computed from
    fs::create_directories(dir);
    const fs::path source = dir / "source.png";
    {
        FILE* f = std::fopen(source.string().c_str(), "wb");
        ASSERT_NE(f, nullptr);
        std::fputs("not really a png", f);
        std::fclose(f);
    }

    const std::string key = TextureCache::MakeKey(source.generic_string());
    ASSERT_FALSE(key.empty());

    CompressedImage image;
    EXPECT_FALSE(TextureCache::Load(key, image));       // nothing yet

    std::vector<uint8_t> px(size_t(128) * 128 * 3, 77);
    CompressedImage original;
    ASSERT_TRUE(CompressImage(px.data(), 128, 128, 3, original));
    TextureCache::Store(key, original);

    ASSERT_TRUE(TextureCache::Load(key, image));
    EXPECT_EQ(image.mips, original.mips);

    // A changed source is another key : the old entry is not served
    {
        FILE* f = std::fopen(source.string().c_str(), "ab");
        ASSERT_NE(f, nullptr);
        std::fputs(" and more", f);
        std::fclose(f);
    }
    EXPECT_NE(TextureCache::MakeKey(source.generic_string()), key);

    // No directory, no cache
    TextureCache::SetDirectory("");
    EXPECT_TRUE(TextureCache::MakeKey(source.generic_string()).empty());

    fs::remove_all(dir, ec);
}

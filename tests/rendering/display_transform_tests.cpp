#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "engine/assets/serialization/material/material_asset_refs.hpp"
#include "engine/renderer/rhi/display_transform.hpp"
#include "engine/renderer/rhi/resources/texture/texture_compression.hpp"

using namespace Shard::Engine::Rendering;

TEST(DisplayTransform, SRGBCurveRoundTrips) {
    for (int i = 0; i <= 255; i++)
    {
        const float encoded = float(i) / 255.0f;
        EXPECT_NEAR(LinearToSRGB(SRGBToLinear(encoded)), encoded, 1e-5f) << i;
    }
}

TEST(DisplayTransform, SRGBCurveKnownValues) {
    EXPECT_FLOAT_EQ(LinearToSRGB(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(LinearToSRGB(1.0f), 1.0f);
    EXPECT_NEAR(LinearToSRGB(0.18f), 0.4614f, 1e-3f);          // middle grey of a photograph
    EXPECT_NEAR(SRGBToLinear(0.5f), 0.2140f, 1e-3f);
    EXPECT_NEAR(LinearToSRGB(0.002f), 0.002f * 12.92f, 1e-6f); // the linear segment near black
}

TEST(DisplayTransform, SRGBCurveClamps) {
    EXPECT_FLOAT_EQ(LinearToSRGB(-3.0f), 0.0f);
    EXPECT_FLOAT_EQ(LinearToSRGB(40.0f), 1.0f);
}

TEST(DisplayTransform, TonemappersAreMonotonicAndBounded) {
    for (Tonemapper t : { Tonemapper::None, Tonemapper::ACES, Tonemapper::Neutral, Tonemapper::Reinhard })
    {
        float previous = -1.0f;
        for (float x = 0.0f; x <= 64.0f; x += 0.05f)
        {
            const glm::vec3 mapped = ApplyTonemap(glm::vec3(x), t);
            for (int c = 0; c < 3; c++)
            {
                EXPECT_GE(mapped[c], 0.0f) << TonemapperName(t) << " " << x;
                EXPECT_LE(mapped[c], 1.0f) << TonemapperName(t) << " " << x;
            }
            EXPECT_GE(mapped.g + 1e-5f, previous) << TonemapperName(t) << " is not monotonic at " << x;
            previous = mapped.g;
        }
    }
}

TEST(DisplayTransform, BlackStaysBlackAndBrightGoesToWhite) {
    for (Tonemapper t : { Tonemapper::None, Tonemapper::ACES, Tonemapper::Neutral, Tonemapper::Reinhard })
    {
        const glm::vec3 black = DisplayTransform(glm::vec3(0.0f), DisplaySettings{ t, 0.0f });
        EXPECT_LT(black.x + black.y + black.z, 0.01f) << TonemapperName(t);

        // Reinhard on the luminance only reaches white at infinity, so the bar is "almost"
        const glm::vec3 sun = DisplayTransform(glm::vec3(5000.0f), DisplaySettings{ t, 0.0f });
        EXPECT_GT(sun.x + sun.y + sun.z, 2.9f) << TonemapperName(t);
    }
}

TEST(DisplayTransform, NegativeInputsStayInRange) {
    const glm::vec3 negative = DisplayTransform(glm::vec3(-5.0f, -0.1f, 0.0f), DisplaySettings{});
    for (int c = 0; c < 3; c++)
    {
        EXPECT_GE(negative[c], 0.0f);
        EXPECT_LE(negative[c], 1.0f);
    }
}

TEST(DisplayTransform, NoneIsTheClampedLightThroughTheSRGBCurve) {
    const DisplaySettings none{ Tonemapper::None, 0.0f };
    EXPECT_NEAR(DisplayTransform(glm::vec3(0.5f), none).x, LinearToSRGB(0.5f), 1e-6f);
    EXPECT_NEAR(DisplayTransform(glm::vec3(3.0f), none).x, 1.0f, 1e-6f);
}

TEST(DisplayTransform, ExposureIsInStops) {
    DisplaySettings settings{ Tonemapper::None, 1.0f };
    EXPECT_FLOAT_EQ(settings.ExposureScale(), 2.0f);
    settings.exposure = -2.0f;
    EXPECT_FLOAT_EQ(settings.ExposureScale(), 0.25f);

    // +1 EV on a value of 0.1 equals 0.2 without it
    const DisplaySettings plus{ Tonemapper::None, 1.0f };
    const DisplaySettings flat{ Tonemapper::None, 0.0f };
    EXPECT_NEAR(DisplayTransform(glm::vec3(0.1f), plus).x, DisplayTransform(glm::vec3(0.2f), flat).x, 1e-6f);
}

TEST(DisplayTransform, NeutralLeavesTheMidtonesAlone) {
    // The point of PBR Neutral : below the start of the compression a colour only loses the small offset of its toe
    const glm::vec3 color(0.5f, 0.3f, 0.2f);
    const glm::vec3 mapped = ApplyTonemap(color, Tonemapper::Neutral);
    for (int c = 0; c < 3; c++)
        EXPECT_NEAR(mapped[c], color[c] - 0.04f, 1e-4f);
}

TEST(DisplayTransform, ACESMapsMiddleGreyToAMidTone) {
    const float mid = ApplyTonemap(glm::vec3(0.18f), Tonemapper::ACES).y;
    EXPECT_GT(mid, 0.05f);
    EXPECT_LT(mid, 0.18f);
    // A diffuse white lit at 1.0 does not reach display white : the curve keeps headroom for what is brighter
    const float white = ApplyTonemap(glm::vec3(1.0f), Tonemapper::ACES).y;
    EXPECT_GT(white, 0.5f);
    EXPECT_LT(white, 0.8f);
    EXPECT_GT(ApplyTonemap(glm::vec3(16.0f), Tonemapper::ACES).y, 0.95f);
}

TEST(DisplayTransform, TonemapperNamesRoundTrip) {
    for (Tonemapper t : { Tonemapper::None, Tonemapper::ACES, Tonemapper::Neutral, Tonemapper::Reinhard })
    {
        Tonemapper parsed = Tonemapper::None;
        ASSERT_TRUE(ParseTonemapper(TonemapperName(t), parsed));
        EXPECT_EQ(parsed, t);
    }

    Tonemapper parsed = Tonemapper::Reinhard;
    EXPECT_TRUE(ParseTonemapper("ACES", parsed));       // case does not matter
    EXPECT_EQ(parsed, Tonemapper::ACES);
    EXPECT_FALSE(ParseTonemapper("filmic9000", parsed));
    EXPECT_EQ(parsed, Tonemapper::ACES);                // a failed parse leaves the value
}

TEST(DisplayTransform, SettingsCompareByValue) {
    EXPECT_TRUE(DisplaySettings{} == DisplaySettings{});

    DisplaySettings other;
    other.exposure = 0.5f;
    EXPECT_TRUE(other != DisplaySettings{});

    other = DisplaySettings{};
    other.tonemapper = Tonemapper::Neutral;
    EXPECT_TRUE(other != DisplaySettings{});
}

TEST(ColorTextureSlots, AreRecognisedByName) {
    using Shard::Engine::Serialization::IsColorTextureSlot;
    EXPECT_TRUE(IsColorTextureSlot("albedo"));
    EXPECT_TRUE(IsColorTextureSlot("Albedo"));
    EXPECT_TRUE(IsColorTextureSlot("albedoMap"));
    EXPECT_TRUE(IsColorTextureSlot("diffuse"));
    EXPECT_TRUE(IsColorTextureSlot("emissiveMap"));
    EXPECT_TRUE(IsColorTextureSlot("baseColorTexture"));

    EXPECT_FALSE(IsColorTextureSlot("normalMap"));
    EXPECT_FALSE(IsColorTextureSlot("metallicMap"));
    EXPECT_FALSE(IsColorTextureSlot("roughnessMap"));
    EXPECT_FALSE(IsColorTextureSlot("aoMap"));
    EXPECT_FALSE(IsColorTextureSlot(""));
    EXPECT_FALSE(IsColorTextureSlot("myAlbedo"));       // a prefix, not a substring
}

namespace {
    // A black and white checkerboard of 1 pixel squares
    std::vector<uint8_t> Checkerboard(uint32_t size)
    {
        std::vector<uint8_t> px(size * size * 4);
        for (uint32_t y = 0; y < size; y++)
            for (uint32_t x = 0; x < size; x++)
            {
                const uint8_t v = ((x + y) & 1) ? 255 : 0;
                uint8_t* p = &px[(y * size + x) * 4];
                p[0] = p[1] = p[2] = v;
                p[3] = 255;
            }
        return px;
    }

    // The red of the first texel of the last (1x1) mip of a compressed image, -1 if its block cannot be decoded
    int LastMipRed(const CompressedImage& image)
    {
        uint8_t rgba[64];
        if (!DecodeBC7Block(image.mips.back().data(), rgba))
            return -1;
        return rgba[0];
    }
}

TEST(TextureMips, ColourTexturesAverageInLinearLight) {
    const uint32_t size = 16;
    const std::vector<uint8_t> px = Checkerboard(size);

    CompressedImage linear, srgb;
    ASSERT_TRUE(CompressImage(px.data(), size, size, 4, linear, false, 1, false));
    ASSERT_TRUE(CompressImage(px.data(), size, size, 4, srgb, false, 1, true));

    // Half the light of white is 188 once encoded (0.5 linear -> 0.735 sRGB), 128 if the encoded values were averaged
    const int linearMip = LastMipRed(linear);
    const int srgbMip = LastMipRed(srgb);
    ASSERT_GE(linearMip, 0);
    ASSERT_GE(srgbMip, 0);
    EXPECT_NEAR(linearMip, 128, 6);
    EXPECT_NEAR(srgbMip, 188, 6);
    EXPECT_GT(srgbMip, linearMip + 40);
}

TEST(TextureCache, KeyOfAColourTextureDiffersFromALinearOne) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "shard_texture_cache_srgb_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    TextureCache::SetDirectory((dir / "textures").generic_string());

    const fs::path source = dir / "source.png";
    {
        FILE* f = std::fopen(source.string().c_str(), "wb");
        ASSERT_NE(f, nullptr);
        std::fputs("not really a png", f);
        std::fclose(f);
    }

    const std::string linearKey = TextureCache::MakeKey(source.generic_string());
    const std::string srgbKey = TextureCache::MakeKey(source.generic_string(), true);
    ASSERT_FALSE(linearKey.empty());
    ASSERT_FALSE(srgbKey.empty());
    EXPECT_NE(linearKey, srgbKey);
    EXPECT_EQ(linearKey, TextureCache::MakeKey(source.generic_string(), false));

    TextureCache::SetDirectory("");
    fs::remove_all(dir, ec);
}

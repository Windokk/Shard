#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <vector>

#include "engine/renderer/rhi/resources/texture/texture_settings.hpp"

using namespace Shard::Engine::Rendering;
namespace fs = std::filesystem;

TEST(TextureSettings, PowerOfTwoHelpers)
{
    EXPECT_TRUE(IsPowerOfTwo(1));
    EXPECT_TRUE(IsPowerOfTwo(512));
    EXPECT_FALSE(IsPowerOfTwo(0));
    EXPECT_FALSE(IsPowerOfTwo(300));

    EXPECT_EQ(NextPowerOfTwo(300), 512u);
    EXPECT_EQ(NextPowerOfTwo(512), 512u);
    EXPECT_EQ(PreviousPowerOfTwo(300), 256u);
    EXPECT_EQ(PreviousPowerOfTwo(512), 512u);
    EXPECT_EQ(NearestPowerOfTwo(300), 256u);
    EXPECT_EQ(NearestPowerOfTwo(400), 512u);
    EXPECT_EQ(NextPowerOfTwo(0), 1u);
}

TEST(TextureSettings, ResolveSize)
{
    TextureImportSettings settings;
    uint32_t w = 0, h = 0;

    ResolveTextureSize(settings, 300, 100, w, h);
    EXPECT_EQ(w, 300u);
    EXPECT_EQ(h, 100u);

    settings.sizeMode = TextureSizeMode::NextPowerOfTwo;
    ResolveTextureSize(settings, 300, 100, w, h);
    EXPECT_EQ(w, 512u);
    EXPECT_EQ(h, 128u);

    settings.sizeMode = TextureSizeMode::Custom;
    settings.customWidth = 64;
    settings.customHeight = 32;
    ResolveTextureSize(settings, 300, 100, w, h);
    EXPECT_EQ(w, 64u);
    EXPECT_EQ(h, 32u);

    settings.sizeMode = TextureSizeMode::Original;
    settings.maxSize = 128;
    ResolveTextureSize(settings, 512, 256, w, h);
    EXPECT_EQ(w, 128u);
    EXPECT_EQ(h, 64u);
}

TEST(TextureSettings, ResizeKeepsFlatColour)
{
    std::vector<unsigned char> pixels(7 * 5 * 4, 200);
    ASSERT_TRUE(ResizeImage(pixels, 7, 5, 4, 16, 8));
    ASSERT_EQ(pixels.size(), size_t(16 * 8 * 4));
    for (unsigned char value : pixels)
        EXPECT_EQ(value, 200);

    ASSERT_TRUE(ResizeImage(pixels, 16, 8, 4, 3, 2));
    ASSERT_EQ(pixels.size(), size_t(3 * 2 * 4));
    for (unsigned char value : pixels)
        EXPECT_EQ(value, 200);
}

TEST(TextureSettings, ResizeRejectsBadInput)
{
    std::vector<unsigned char> pixels(10);
    EXPECT_FALSE(ResizeImage(pixels, 4, 4, 4, 8, 8));
    EXPECT_FALSE(ResizeImage(pixels, 0, 4, 4, 8, 8));
}

TEST(TextureSettings, SaveAndLoadRoundTrip)
{
    const fs::path dir = fs::temp_directory_path() / "shard_texture_settings_test";
    fs::create_directories(dir);
    const Shard::Engine::Filesystem::Path image((dir / "image.png").generic_string());

    TextureImportSettings settings;
    settings.sizeMode = TextureSizeMode::Custom;
    settings.customWidth = 96;
    settings.customHeight = 48;
    settings.maxSize = 1024;
    settings.compress = false;
    settings.generateMips = false;
    settings.minFilter = TextureFilter::Nearest;
    settings.magFilter = TextureFilter::Nearest;
    settings.wrapS = TextureWrap::ClampEdge;
    settings.wrapT = TextureWrap::Mirror;

    ASSERT_TRUE(SaveTextureSettings(image, settings));
    EXPECT_TRUE(fs::exists(image.full + kTextureSettingsExtension));
    EXPECT_EQ(LoadTextureSettings(image), settings);

    ASSERT_TRUE(SaveTextureSettings(image, TextureImportSettings()));
    EXPECT_FALSE(fs::exists(image.full + kTextureSettingsExtension));
    EXPECT_EQ(LoadTextureSettings(image), TextureImportSettings());

    fs::remove_all(dir);
}

TEST(TextureSettings, SettingsFileNames)
{
    EXPECT_TRUE(IsTextureSettingsFile("a/b.png.texsettings"));
    EXPECT_FALSE(IsTextureSettingsFile("a/b.png"));
    EXPECT_EQ(ImagePathOfSettingsFile("a/b.png.texsettings"), "a/b.png");
}

TEST(TextureSettings, ApplyFallsBackWithoutMips)
{
    TextureImportSettings settings;
    settings.generateMips = false;

    TextureSpecifications specs;
    ApplyTextureSettings(settings, specs);
    EXPECT_FALSE(specs.generateMips);
    EXPECT_EQ(specs.minFilter, TextureFilter::Linear);
}

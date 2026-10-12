#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

#include "engine/core/math/rgb9e5.hpp"
#include "engine/renderer/features/lighting/lightmap_data.hpp"
#include "engine/renderer/features/lighting/lightmap_layout.hpp"

using namespace Shard::Engine::Rendering;
using Shard::Engine::Core::PackRGB9E5;
using Shard::Engine::Core::UnpackRGB9E5;

// ---- RGB9E5 ----

TEST(RGB9E5, RoundTripsWithinItsPrecision) {
    const float values[][3] = {
        { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.5f, 0.25f, 0.125f }, { 3.7f, 0.01f, 1.2f }, { 100.0f, 50.0f, 0.3f },
        { 0.0003f, 0.0001f, 0.0002f }, { 65000.0f, 1.0f, 0.0f }, { 0.9999f, 0.9999f, 0.9999f },
    };

    for (const auto& v : values)
    {
        float r, g, b;
        UnpackRGB9E5(PackRGB9E5(v[0], v[1], v[2]), r, g, b);

        // 9 bits of mantissa : within 1/512 of the largest channel (the others share its exponent)
        const float largest = std::max(v[0], std::max(v[1], v[2]));
        const float tolerance = largest / 256.0f + 1e-7f;
        EXPECT_NEAR(r, v[0], tolerance);
        EXPECT_NEAR(g, v[1], tolerance);
        EXPECT_NEAR(b, v[2], tolerance);
    }
}

TEST(RGB9E5, ClampsWhatItCannotHold) {
    float r, g, b;
    UnpackRGB9E5(PackRGB9E5(-1.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f), r, g, b);
    EXPECT_EQ(r, 0.0f);
    EXPECT_EQ(g, 0.0f);
    EXPECT_EQ(b, 0.0f);

    UnpackRGB9E5(PackRGB9E5(1e9f, 1.0f, 1.0f), r, g, b);
    EXPECT_NEAR(r, 65408.0f, 1.0f);
    EXPECT_TRUE(std::isfinite(r));
}

TEST(RGB9E5, MatchesTheKnownEncodingOfOne) {
    // The GL specification : (1, 1, 1) is exponent 16, mantissa 256 on every channel
    EXPECT_EQ(PackRGB9E5(1.0f, 1.0f, 1.0f), (16u << 27) | (256u << 18) | (256u << 9) | 256u);
}

// ---- The file ----

namespace {

    LightmapData MakeData()
    {
        LightmapData data;
        for (uint32_t p = 0; p < 3; p++)
        {
            LightmapPageData page;
            page.width = 16 + 8 * p;
            page.height = 8 + 4 * p;
            for (uint32_t i = 0; i < page.width * page.height; i++)
                page.texels.push_back(PackRGB9E5(float(i % 7), float(i % 5) * 0.5f, float(p)));
            data.pages.push_back(std::move(page));
        }
        return data;
    }

    class LightmapFile : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
            path = Shard::Engine::Filesystem::Path(
                (std::filesystem::temp_directory_path() / (std::string("shard_lightmap_") + info->name() + ".lightmap")).generic_string(), true);
            std::filesystem::remove(path.full);
        }
        void TearDown() override { std::filesystem::remove(path.full); }

        std::vector<char> Bytes() const
        {
            std::ifstream in(path.full, std::ios::binary);
            return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }
        void Overwrite(const std::vector<char>& bytes) const
        {
            std::ofstream out(path.full, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), std::streamsize(bytes.size()));
        }

        Shard::Engine::Filesystem::Path path;
    };
}

TEST_F(LightmapFile, RoundTripPreservesEverything) {
    const LightmapData original = MakeData();
    ASSERT_TRUE(WriteLightmapFile(path, original));

    const std::shared_ptr<LightmapData> loaded = DecodeLightmapFile(path);
    ASSERT_NE(loaded, nullptr);
    ASSERT_EQ(loaded->pages.size(), original.pages.size());
    for (size_t p = 0; p < original.pages.size(); p++)
    {
        EXPECT_EQ(loaded->pages[p].width, original.pages[p].width);
        EXPECT_EQ(loaded->pages[p].height, original.pages[p].height);
        EXPECT_EQ(loaded->pages[p].texels, original.pages[p].texels);
    }
}

TEST_F(LightmapFile, AnEmptyBakeRoundTrips) {
    ASSERT_TRUE(WriteLightmapFile(path, LightmapData{}));
    const std::shared_ptr<LightmapData> loaded = DecodeLightmapFile(path);
    ASSERT_NE(loaded, nullptr);
    EXPECT_TRUE(loaded->pages.empty());
}

TEST_F(LightmapFile, WriteCreatesMissingDirectories) {
    const auto nested = Shard::Engine::Filesystem::Path((std::filesystem::temp_directory_path() / "shard_lightmap_dir" / "a" / "b.lightmap").generic_string(), true);
    ASSERT_TRUE(WriteLightmapFile(nested, MakeData()));
    EXPECT_NE(DecodeLightmapFile(nested), nullptr);
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "shard_lightmap_dir");
}

TEST_F(LightmapFile, WriteRefusesAPageThatContradictsItsSize) {
    LightmapData bad = MakeData();
    bad.pages[1].texels.pop_back();
    EXPECT_FALSE(WriteLightmapFile(path, bad));

    bad = MakeData();
    bad.pages[0].width = 0;
    EXPECT_FALSE(WriteLightmapFile(path, bad));
}

TEST_F(LightmapFile, DamagedFilesAreRejected) {
    EXPECT_EQ(DecodeLightmapFile(path), nullptr) << "missing";

    ASSERT_TRUE(WriteLightmapFile(path, MakeData()));
    const std::vector<char> good = Bytes();

    for (size_t keep : { size_t(0), size_t(3), size_t(15), good.size() / 2, good.size() - 1 })
    {
        Overwrite(std::vector<char>(good.begin(), good.begin() + std::ptrdiff_t(keep)));
        EXPECT_EQ(DecodeLightmapFile(path), nullptr) << "kept " << keep << " bytes";
    }

    std::vector<char> garbage = good;
    garbage.push_back('x');
    Overwrite(garbage);
    EXPECT_EQ(DecodeLightmapFile(path), nullptr) << "trailing garbage";

    std::vector<char> wrongMagic = good;
    wrongMagic[0] = 'X';
    Overwrite(wrongMagic);
    EXPECT_EQ(DecodeLightmapFile(path), nullptr) << "wrong magic";

    std::vector<char> wrongVersion = good;
    wrongVersion[4] = 99;
    Overwrite(wrongVersion);
    EXPECT_EQ(DecodeLightmapFile(path), nullptr) << "unknown version";

    std::vector<char> hugePage = good;
    hugePage[16] = hugePage[17] = hugePage[18] = char(0x7F);        // the first page's width
    Overwrite(hugePage);
    EXPECT_EQ(DecodeLightmapFile(path), nullptr) << "a page the size of the universe";
}

// ---- Encoding a baked page ----

TEST(LightmapEncode, TheGutterTakesTheLightOfTheSurfaceNextToIt) {
    // 8 x 4 page : only column 3 is covered, lit with 2.0 on the top row and 4.0 on the others
    const uint32_t w = 8, h = 4;
    std::vector<float> rgba(size_t(w) * h * 4, 0.0f);
    for (uint32_t y = 0; y < h; y++)
    {
        const size_t i = (size_t(y) * w + 3) * 4;
        rgba[i] = rgba[i + 1] = rgba[i + 2] = (y == 0) ? 2.0f : 4.0f;
        rgba[i + 3] = 1.0f;
    }

    const LightmapPageData page = EncodeLightmapPage(w, h, rgba.data(), 2);
    ASSERT_EQ(page.texels.size(), size_t(w) * h);

    auto texel = [&](uint32_t x, uint32_t y) {
        float r, g, b;
        UnpackRGB9E5(page.texels[size_t(y) * w + x], r, g, b);
        return r;
    };

    EXPECT_NEAR(texel(3, 1), 4.0f, 0.02f);          // the baked texels are as they were
    EXPECT_GT(texel(2, 1), 0.0f);                   // one ring of gutter on each side...
    EXPECT_GT(texel(4, 1), 0.0f);
    EXPECT_GT(texel(1, 1), 0.0f);                   // ...and a second one
    EXPECT_GT(texel(5, 1), 0.0f);
    EXPECT_EQ(texel(0, 1), 0.0f);                   // three away from the surface : nothing
    EXPECT_EQ(texel(7, 1), 0.0f);
    // The average of the three covered texels it touches (2, 4 and 4) : the light of the surface, not a darkened share of it
    EXPECT_NEAR(texel(2, 1), (2.0f + 4.0f + 4.0f) / 3.0f, 0.05f);
}

TEST(LightmapEncode, NonFiniteLightDoesNotPoisonTheNeighbours) {
    std::vector<float> rgba(4 * 4 * 4, 0.0f);
    auto set = [&](uint32_t x, uint32_t y, float v) {
        const size_t i = (size_t(y) * 4 + x) * 4;
        rgba[i] = rgba[i + 1] = rgba[i + 2] = v;
        rgba[i + 3] = 1.0f;
    };
    set(1, 1, std::numeric_limits<float>::quiet_NaN());
    set(2, 1, 1.0f);

    const LightmapPageData page = EncodeLightmapPage(4, 4, rgba.data(), 1);
    for (uint32_t texel : page.texels)
    {
        float r, g, b;
        UnpackRGB9E5(texel, r, g, b);
        EXPECT_TRUE(std::isfinite(r) && std::isfinite(g) && std::isfinite(b));
    }
}

// ---- The atlas layout ----

TEST(LightmapLayout, ResolutionFollowsTheDensityAndTheCoverage) {
    LightmapSettings settings;
    settings.texelsPerMeter = 16.0f;
    settings.minResolution = 8;
    settings.maxResolution = 512;

    LightmapInstanceInput instance;
    instance.surfaceArea = 16.0f;       // 4 m x 4 m of surface...
    instance.uvArea = 1.0f;             // ...laid out on the whole square : 4 m * 16 = 64 texels
    EXPECT_EQ(LightmapResolutionFor(instance, settings), 64u);

    instance.uvArea = 0.25f;            // a layout that wastes three quarters of the square needs twice the resolution
    EXPECT_EQ(LightmapResolutionFor(instance, settings), 128u);

    instance.uvArea = 1e-9f;            // an absurd layout is no reason to ask for the moon
    EXPECT_EQ(LightmapResolutionFor(instance, settings), settings.minResolution);

    instance.surfaceArea = 1e6f;
    instance.uvArea = 0.5f;
    EXPECT_EQ(LightmapResolutionFor(instance, settings), 512u) << "clamped to the largest";

    instance.resolutionOverride = 100;
    EXPECT_EQ(LightmapResolutionFor(instance, settings), 100u);
    instance.resolutionOverride = 100000;
    EXPECT_EQ(LightmapResolutionFor(instance, settings), 512u) << "an override is clamped too";
}

TEST(LightmapLayout, PacksEveryInstanceInsideOnePageWithoutOverlapOrContact) {
    LightmapSettings settings;
    std::vector<LightmapInstanceInput> instances;
    for (int i = 0; i < 40; i++)
    {
        LightmapInstanceInput instance;
        instance.key = 1000 + uint64_t(i);
        instance.resolutionOverride = 16 + uint32_t((i * 37) % 90);
        instances.push_back(instance);
    }

    const LightmapLayout layout = PlanLightmapLayout(instances, settings);
    ASSERT_EQ(layout.placements.size(), instances.size());
    EXPECT_EQ(layout.pageCount, 1u);
    EXPECT_GE(layout.pageSize, 16u);
    EXPECT_EQ(layout.pageSize & (layout.pageSize - 1), 0u) << "a power of two";

    for (size_t i = 0; i < layout.placements.size(); i++)
    {
        const LightmapPlacement& a = layout.placements[i];
        EXPECT_EQ(a.key, instances[i].key);
        EXPECT_EQ(a.resolution, instances[i].resolutionOverride);
        EXPECT_LE(a.x + a.resolution, layout.pageSize);
        EXPECT_LE(a.y + a.resolution, layout.pageSize);

        for (size_t j = i + 1; j < layout.placements.size(); j++)
        {
            const LightmapPlacement& b = layout.placements[j];
            if (a.page != b.page)
                continue;
            // At least the gap of both between them
            const bool apart = a.x + a.resolution + 2 <= b.x || b.x + b.resolution + 2 <= a.x ||
                               a.y + a.resolution + 2 <= b.y || b.y + b.resolution + 2 <= a.y;
            EXPECT_TRUE(apart) << i << " and " << j;
        }
    }
}

TEST(LightmapLayout, TheScaleAndOffsetMapTheSquareOntoItsPlace) {
    LightmapPlacement placement;
    placement.x = 64;
    placement.y = 128;
    placement.resolution = 256;

    const glm::vec4 scaleOffset = placement.ScaleOffset(1024);
    EXPECT_FLOAT_EQ(scaleOffset.x, 0.25f);
    EXPECT_FLOAT_EQ(scaleOffset.y, 0.25f);
    EXPECT_FLOAT_EQ(scaleOffset.z, 0.0625f);
    EXPECT_FLOAT_EQ(scaleOffset.w, 0.125f);

    // The corners of the square land on the corners of the place
    EXPECT_FLOAT_EQ(1.0f * scaleOffset.x + scaleOffset.z, (64.0f + 256.0f) / 1024.0f);
    EXPECT_FLOAT_EQ(1.0f * scaleOffset.y + scaleOffset.w, (128.0f + 256.0f) / 1024.0f);
}

TEST(LightmapLayout, SpillsToSeveralPagesWhenOneIsNotEnough) {
    LightmapSettings settings;
    settings.maxPageSize = 256;
    settings.maxResolution = 200;

    std::vector<LightmapInstanceInput> instances(6);
    for (size_t i = 0; i < instances.size(); i++)
    {
        instances[i].key = i;
        instances[i].resolutionOverride = 200;
    }

    const LightmapLayout layout = PlanLightmapLayout(instances, settings);
    EXPECT_EQ(layout.pageSize, 256u);
    EXPECT_EQ(layout.pageCount, 6u) << "a 200 texel square leaves no room for another on a 256 page";
    for (const LightmapPlacement& placement : layout.placements)
        EXPECT_LT(placement.page, layout.pageCount);
}

TEST(LightmapLayout, IsDeterministicAndHandlesNothing) {
    EXPECT_EQ(PlanLightmapLayout({}, LightmapSettings{}).pageCount, 0u);

    std::vector<LightmapInstanceInput> instances(25);
    for (size_t i = 0; i < instances.size(); i++)
    {
        instances[i].key = i;
        instances[i].surfaceArea = 1.0f + float(i % 6);
        instances[i].uvArea = 0.4f;
    }

    const LightmapLayout a = PlanLightmapLayout(instances, LightmapSettings{});
    const LightmapLayout b = PlanLightmapLayout(instances, LightmapSettings{});
    ASSERT_EQ(a.placements.size(), b.placements.size());
    for (size_t i = 0; i < a.placements.size(); i++)
    {
        EXPECT_EQ(a.placements[i].page, b.placements[i].page);
        EXPECT_EQ(a.placements[i].x, b.placements[i].x);
        EXPECT_EQ(a.placements[i].y, b.placements[i].y);
        EXPECT_EQ(a.placements[i].resolution, b.placements[i].resolution);
    }
}

// ---- Drawing a lightmap layout into a page ----

#include "engine/renderer/features/lighting/lightmap_raster.hpp"

#include <cstring>

#include <glm/gtc/matrix_transform.hpp>

namespace {

    // A quad in the xz plane facing +y, whose lightmap coordinates are its xz coordinates, as the 8 float vertices the
    // rasterizer reads : position (3), normal (3), lightmap uv (2)
    struct QuadMesh
    {
        std::vector<float> vertices;
        std::vector<uint32_t> indices = { 0, 2, 1, 1, 2, 3 };
        QuadMesh()
        {
            auto vertex = [&](float x, float z) {
                const float v[8] = { x, 0.0f, z, 0.0f, 1.0f, 0.0f, x, z };
                vertices.insert(vertices.end(), v, v + 8);
            };
            vertex(0, 0); vertex(1, 0); vertex(0, 1); vertex(1, 1);
        }

        LightmapRasterInstance Instance(const glm::mat4& world, uint32_t x, uint32_t y, uint32_t resolution) const
        {
            LightmapRasterInstance instance;
            instance.vertices = reinterpret_cast<const uint8_t*>(vertices.data());
            instance.vertexStride = 8 * sizeof(float);
            instance.positionOffset = 0;
            instance.normalOffset = 3 * sizeof(float);
            instance.lightmapUVOffset = 6 * sizeof(float);
            instance.indices = indices.data();
            instance.indexCount = indices.size();
            instance.worldMatrix = world;
            instance.x = x;
            instance.y = y;
            instance.resolution = resolution;
            return instance;
        }
    };
}

TEST(LightmapRaster, EveryTexelOfALightmapGetsItsPointOfTheSurface) {
    QuadMesh quad;
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
    const std::shared_ptr<LightmapPageRaster> raster = RasterizeLightmapPage(16, { quad.Instance(world, 2, 3, 8) });
    ASSERT_NE(raster, nullptr);
    ASSERT_EQ(raster->size, 16u);

    // The 8 x 8 texels of the place, and nothing else
    EXPECT_EQ(raster->texelList.size(), size_t(64));
    for (uint32_t y = 0; y < 16; y++)
        for (uint32_t x = 0; x < 16; x++)
            EXPECT_EQ(raster->IsCovered(x, y), x >= 2 && x < 10 && y >= 3 && y < 11) << x << "," << y;

    for (uint32_t y = 3; y < 11; y++)
    {
        for (uint32_t x = 2; x < 10; x++)
        {
            const size_t i = (size_t(y) * 16 + x) * 4;

            // The centre of the texel, mapped onto the quad and moved to where the instance stands
            const float u = (float(x) + 0.5f - 2.0f) / 8.0f, v = (float(y) + 0.5f - 3.0f) / 8.0f;
            EXPECT_NEAR(raster->position[i + 0], 1.0f + 2.0f * u, 1e-4f);
            EXPECT_NEAR(raster->position[i + 1], 2.0f, 1e-4f);
            EXPECT_NEAR(raster->position[i + 2], 3.0f + 2.0f * v, 1e-4f);

            EXPECT_NEAR(raster->normal[i + 1], 1.0f, 1e-5f);
            EXPECT_NEAR(raster->geometric[i + 1], 1.0f, 1e-5f);

            // The quad is 2 m wide on 8 texels : a texel is a quarter of a meter
            EXPECT_NEAR(raster->position[i + 3], 0.25f, 1e-4f);
        }
    }

    // The list is the covered texels, row after row, as (y << 16) | x
    for (size_t k = 1; k < raster->texelList.size(); k++)
        EXPECT_LT(raster->texelList[k - 1], raster->texelList[k]);
    EXPECT_EQ(raster->texelList.front(), (3u << 16) | 2u);
}

TEST(LightmapRaster, TheGeometricNormalFollowsTheSideTheSmoothNormalFaces) {
    QuadMesh quad;
    // Flip the smooth normals : the triangles still wind counter clockwise seen from +y, the surface now faces -y
    for (size_t v = 0; v < 4; v++)
        quad.vertices[v * 8 + 4] = -1.0f;

    const std::shared_ptr<LightmapPageRaster> raster = RasterizeLightmapPage(16, { quad.Instance(glm::mat4(1.0f), 0, 0, 16) });
    const size_t i = (size_t(5) * 16 + 5) * 4;
    EXPECT_NEAR(raster->normal[i + 1], -1.0f, 1e-5f);
    EXPECT_NEAR(raster->geometric[i + 1], -1.0f, 1e-5f);
}

TEST(LightmapRaster, ATriangleTooThinForATexelCentreStillGetsATexel) {
    // A sliver 3 texels long and a tenth of a texel wide, between two rows of texel centres
    std::vector<float> vertices;
    auto vertex = [&](float x, float z, float u, float v) {
        const float vertexData[8] = { x, 0.0f, z, 0.0f, 1.0f, 0.0f, u, v };
        vertices.insert(vertices.end(), vertexData, vertexData + 8);
    };
    vertex(0.0f, 0.0f, 0.0f, 0.0f);
    vertex(3.0f, 0.0f, 3.0f / 16.0f, 0.0f);
    vertex(3.0f, 0.1f, 3.0f / 16.0f, 0.1f / 16.0f);
    std::vector<uint32_t> indices = { 0, 2, 1 };

    LightmapRasterInstance instance;
    instance.vertices = reinterpret_cast<const uint8_t*>(vertices.data());
    instance.vertexStride = 8 * sizeof(float);
    instance.normalOffset = 3 * sizeof(float);
    instance.lightmapUVOffset = 6 * sizeof(float);
    instance.indices = indices.data();
    instance.indexCount = indices.size();
    instance.resolution = 16;

    const std::shared_ptr<LightmapPageRaster> raster = RasterizeLightmapPage(16, { instance });
    EXPECT_GE(raster->texelList.size(), size_t(1)) << "a triangle that covers no texel centre is not left without light";
}

TEST(LightmapRaster, IgnoresWhatItCannotUse) {
    QuadMesh quad;
    LightmapRasterInstance noResolution = quad.Instance(glm::mat4(1.0f), 0, 0, 0);
    LightmapRasterInstance noVertices = quad.Instance(glm::mat4(1.0f), 0, 0, 8);
    noVertices.vertices = nullptr;

    const std::shared_ptr<LightmapPageRaster> raster = RasterizeLightmapPage(8, { noResolution, noVertices });
    EXPECT_TRUE(raster->texelList.empty());

    // A lightmap that sticks out of the page is cut at its edge
    const std::shared_ptr<LightmapPageRaster> clipped = RasterizeLightmapPage(8, { quad.Instance(glm::mat4(1.0f), 4, 4, 8) });
    EXPECT_EQ(clipped->texelList.size(), size_t(16));
}

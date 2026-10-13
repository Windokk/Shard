#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/math/rect_packer.hpp"
#include "engine/renderer/rhi/resources/mesh/lightmap_uv.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"

using namespace Shard::Engine::Rendering;
using Shard::Engine::Core::PackRect;
using Shard::Engine::Core::PackRectsShelf;

namespace {

    struct SoupMesh
    {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        std::vector<uint32_t> indices;

        LightmapUnwrapGroup Group() const
        {
            LightmapUnwrapGroup group;
            group.positions = positions.data();
            group.normals = normals.data();
            group.vertexCount = positions.size();
            group.indices = indices.data();
            group.indexCount = indices.size();
            return group;
        }
    };

    // A cube with its 8 corners shared by the faces (smooth normals) : the unwrapper has to cut it along the edges anyway
    SoupMesh MakeSharedCube(float size)
    {
        SoupMesh mesh;
        for (int i = 0; i < 8; i++)
        {
            glm::vec3 p((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
            mesh.positions.push_back(p * size * 0.5f);
            mesh.normals.push_back(glm::normalize(p));
        }

        // Each face as two triangles, counter clockwise seen from outside
        const uint32_t faces[6][4] = {
            { 1, 3, 7, 5 }, { 0, 4, 6, 2 },     // +x, -x
            { 2, 6, 7, 3 }, { 0, 1, 5, 4 },     // +y, -y
            { 4, 5, 7, 6 }, { 0, 2, 3, 1 },     // +z, -z
        };
        for (const auto& f : faces)
        {
            mesh.indices.insert(mesh.indices.end(), { f[0], f[1], f[2], f[0], f[2], f[3] });
        }
        return mesh;
    }

    // A flat grid of n x n quads in the xz plane, `size` wide
    SoupMesh MakeGrid(int n, float size)
    {
        SoupMesh mesh;
        for (int z = 0; z <= n; z++)
            for (int x = 0; x <= n; x++)
            {
                mesh.positions.emplace_back((float(x) / n - 0.5f) * size, 0.0f, (float(z) / n - 0.5f) * size);
                mesh.normals.emplace_back(0.0f, 1.0f, 0.0f);
            }
        for (int z = 0; z < n; z++)
            for (int x = 0; x < n; x++)
            {
                const uint32_t i = uint32_t(z * (n + 1) + x);
                mesh.indices.insert(mesh.indices.end(), { i, i + uint32_t(n) + 1, i + 1, i + 1, i + uint32_t(n) + 1, i + uint32_t(n) + 2 });
            }
        return mesh;
    }

    SoupMesh MakeSphere(int rings, int sectors, float radius)
    {
        SoupMesh mesh;
        for (int r = 0; r <= rings; r++)
            for (int s = 0; s <= sectors; s++)
            {
                const float theta = glm::pi<float>() * float(r) / rings;
                const float phi = 2.0f * glm::pi<float>() * float(s) / sectors;
                const glm::vec3 n(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
                mesh.positions.push_back(n * radius);
                mesh.normals.push_back(n);
            }
        for (int r = 0; r < rings; r++)
            for (int s = 0; s < sectors; s++)
            {
                const uint32_t i = uint32_t(r * (sectors + 1) + s);
                const uint32_t j = i + uint32_t(sectors) + 1;
                mesh.indices.insert(mesh.indices.end(), { i, j, i + 1, i + 1, j, j + 1 });
            }
        return mesh;
    }

    float TriangleArea(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c)
    {
        return 0.5f * std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
    }

    // How many triangles of the layout cover each texel centre of a resolution x resolution grid, the centres exactly on an
    // edge or a corner counting for none. More than one anywhere means two parts of the surface share the same texels.
    int MaxCoverage(const std::vector<LightmapUnwrapGroupResult>& groups, int resolution)
    {
        std::vector<int> coverage(size_t(resolution) * resolution, 0);
        int maxCoverage = 0;

        for (const LightmapUnwrapGroupResult& group : groups)
        {
            for (size_t t = 0; t + 2 < group.indices.size(); t += 3)
            {
                const glm::vec2 a = group.uv[group.indices[t]] * float(resolution);
                const glm::vec2 b = group.uv[group.indices[t + 1]] * float(resolution);
                const glm::vec2 c = group.uv[group.indices[t + 2]] * float(resolution);
                const float area2 = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                if (std::abs(area2) < 1e-6f)
                    continue;

                const int x0 = std::max(0, int(std::floor(std::min({ a.x, b.x, c.x }))));
                const int x1 = std::min(resolution - 1, int(std::ceil(std::max({ a.x, b.x, c.x }))));
                const int y0 = std::max(0, int(std::floor(std::min({ a.y, b.y, c.y }))));
                const int y1 = std::min(resolution - 1, int(std::ceil(std::max({ a.y, b.y, c.y }))));

                for (int y = y0; y <= y1; y++)
                    for (int x = x0; x <= x1; x++)
                    {
                        const glm::vec2 p(float(x) + 0.5f, float(y) + 0.5f);
                        const float w0 = ((b.x - p.x) * (c.y - p.y) - (b.y - p.y) * (c.x - p.x)) / area2;
                        const float w1 = ((c.x - p.x) * (a.y - p.y) - (c.y - p.y) * (a.x - p.x)) / area2;
                        const float w2 = 1.0f - w0 - w1;
                        if (w0 > 1e-4f && w1 > 1e-4f && w2 > 1e-4f)
                            maxCoverage = std::max(maxCoverage, ++coverage[size_t(y) * resolution + x]);
                    }
            }
        }
        return maxCoverage;
    }

    void ExpectValidLayout(const SoupMesh& mesh, const LightmapUnwrapResult& result)
    {
        ASSERT_TRUE(result.success);
        ASSERT_EQ(result.groups.size(), size_t(1));
        const LightmapUnwrapGroupResult& group = result.groups[0];

        EXPECT_EQ(group.indices.size(), mesh.indices.size());
        EXPECT_EQ(group.sourceVertex.size(), group.uv.size());

        for (const glm::vec2& uv : group.uv)
        {
            EXPECT_GE(uv.x, 0.0f);
            EXPECT_LE(uv.x, 1.0f);
            EXPECT_GE(uv.y, 0.0f);
            EXPECT_LE(uv.y, 1.0f);
        }
        for (uint32_t source : group.sourceVertex)
            EXPECT_LT(source, mesh.positions.size());
        for (uint32_t index : group.indices)
            EXPECT_LT(index, group.uv.size());
    }

    std::optional<std::string> FindModel(const std::string& name)
    {
        for (const char* prefix : { "../example_project/resources/models/", "example_project/resources/models/" })
        {
            std::filesystem::path path = std::string(prefix) + name;
            if (std::filesystem::exists(path))
                return path.generic_string();
        }
        return std::nullopt;
    }
}

// ---- The packer ----

TEST(RectPacker, PlacesEveryRectInsideItsBinWithoutOverlap) {
    std::vector<PackRect> rects;
    unsigned seed = 12345;
    auto next = [&] { seed = seed * 1664525u + 1013904223u; return float((seed >> 8) & 0xFFFF) / 65535.0f; };
    for (int i = 0; i < 300; i++)
    {
        PackRect r;
        r.w = 0.01f + next() * 0.12f;
        r.h = 0.01f + next() * 0.12f;
        rects.push_back(r);
    }

    const int bins = PackRectsShelf(rects, 1.0f, 1.0f);
    ASSERT_GE(bins, 1);

    for (size_t i = 0; i < rects.size(); i++)
    {
        const PackRect& a = rects[i];
        ASSERT_GE(a.bin, 0);
        ASSERT_LT(a.bin, bins);
        EXPECT_GE(a.x, 0.0f);
        EXPECT_GE(a.y, 0.0f);
        EXPECT_LE(a.x + a.w, 1.0f + 1e-5f);
        EXPECT_LE(a.y + a.h, 1.0f + 1e-5f);

        for (size_t j = i + 1; j < rects.size(); j++)
        {
            const PackRect& b = rects[j];
            if (a.bin != b.bin)
                continue;
            const bool apart = a.x + a.w <= b.x + 1e-5f || b.x + b.w <= a.x + 1e-5f || a.y + a.h <= b.y + 1e-5f || b.y + b.h <= a.y + 1e-5f;
            EXPECT_TRUE(apart) << "rects " << i << " and " << j << " overlap";
        }
    }
}

TEST(RectPacker, IsDeterministic) {
    std::vector<PackRect> a(40), b(40);
    for (size_t i = 0; i < a.size(); i++)
        a[i].w = b[i].w = 0.05f + 0.01f * float(i % 7), a[i].h = b[i].h = 0.04f + 0.01f * float(i % 5);

    ASSERT_EQ(PackRectsShelf(a, 1.0f, 1.0f), PackRectsShelf(b, 1.0f, 1.0f));
    for (size_t i = 0; i < a.size(); i++)
    {
        EXPECT_EQ(a[i].x, b[i].x);
        EXPECT_EQ(a[i].y, b[i].y);
        EXPECT_EQ(a[i].bin, b[i].bin);
    }
}

TEST(RectPacker, OpensNewBinsAndReportsWhatCannotFit) {
    std::vector<PackRect> rects(5);
    for (PackRect& r : rects)
        r.w = r.h = 0.6f;       // one per bin

    EXPECT_EQ(PackRectsShelf(rects, 1.0f, 1.0f), 5);

    rects.resize(5);
    EXPECT_EQ(PackRectsShelf(rects, 1.0f, 1.0f, 3), -1) << "more bins than allowed";

    std::vector<PackRect> tooBig(1);
    tooBig[0].w = 1.5f;
    tooBig[0].h = 0.1f;
    EXPECT_EQ(PackRectsShelf(tooBig, 1.0f, 1.0f), -1);
    EXPECT_EQ(tooBig[0].bin, -1);
}

// ---- The unwrapper ----

TEST(LightmapUnwrap, CubeBecomesSixChartsOfTheSameDensity) {
    const SoupMesh cube = MakeSharedCube(2.0f);
    const LightmapUnwrapResult result = UnwrapForLightmap({ cube.Group() });

    ExpectValidLayout(cube, result);
    EXPECT_EQ(result.chartCount, 6u);
    EXPECT_NEAR(result.surfaceArea, 24.0f, 1e-3f);

    // Flat faces project without distortion : every triangle takes the same share of the square per unit of area, which is
    // what makes a lightmap as sharp on one face as on another
    const LightmapUnwrapGroupResult& group = result.groups[0];
    const float density = result.uvArea / result.surfaceArea;
    for (size_t t = 0; t + 2 < group.indices.size(); t += 3)
    {
        const glm::vec3 p0 = cube.positions[group.sourceVertex[group.indices[t]]];
        const glm::vec3 p1 = cube.positions[group.sourceVertex[group.indices[t + 1]]];
        const glm::vec3 p2 = cube.positions[group.sourceVertex[group.indices[t + 2]]];
        const float area = 0.5f * glm::length(glm::cross(p1 - p0, p2 - p0));
        const float uvArea = TriangleArea(group.uv[group.indices[t]], group.uv[group.indices[t + 1]], group.uv[group.indices[t + 2]]);
        EXPECT_NEAR(uvArea / area, density, density * 1e-3f);
    }

    // The corners shared by three faces were cut into one vertex per chart
    EXPECT_EQ(group.sourceVertex.size(), size_t(24));
    EXPECT_EQ(MaxCoverage(result.groups, 512), 1);
}

TEST(LightmapUnwrap, ScaleDoesNotChangeTheLayout) {
    // Only the density follows the size : a cube twice as big is laid out the same way, in the same space
    const LightmapUnwrapResult small = UnwrapForLightmap({ MakeSharedCube(1.0f).Group() });
    const LightmapUnwrapResult big = UnwrapForLightmap({ MakeSharedCube(4.0f).Group() });

    ASSERT_TRUE(small.success && big.success);
    EXPECT_NEAR(small.uvArea, big.uvArea, 1e-3f);
    EXPECT_NEAR(big.surfaceArea, small.surfaceArea * 16.0f, 1e-3f);
    ASSERT_EQ(small.groups[0].uv.size(), big.groups[0].uv.size());
    for (size_t i = 0; i < small.groups[0].uv.size(); i++)
        EXPECT_NEAR(glm::distance(small.groups[0].uv[i], big.groups[0].uv[i]), 0.0f, 1e-3f);
}

TEST(LightmapUnwrap, AFlatGridIsOneChart) {
    const SoupMesh grid = MakeGrid(20, 10.0f);
    const LightmapUnwrapResult result = UnwrapForLightmap({ grid.Group() });

    ExpectValidLayout(grid, result);
    EXPECT_EQ(result.chartCount, 1u);
    EXPECT_EQ(result.groups[0].sourceVertex.size(), grid.positions.size()) << "nothing to cut in a single chart";
    EXPECT_EQ(MaxCoverage(result.groups, 512), 1);

    // A square chart fills most of the unit square (what is left is the padding)
    EXPECT_GT(result.uvArea, 0.9f);
}

// A cloth modelled with relief at the scale of its own triangles : its face normals jitter by tens of degrees while the
// surface is flat as far as a lightmap can tell. It has to come out as a few charts, not as a heap of slivers.
TEST(LightmapUnwrap, ANoisyFineSurfaceDoesNotShatterIntoSlivers) {
    SoupMesh mesh = MakeGrid(100, 1.0f);     // 1 cm triangles
    uint32_t state = 12345u;
    for (glm::vec3& p : mesh.positions)
    {
        state = state * 1664525u + 1013904223u;
        p.y = (float((state >> 8) & 0xFFFF) / 65535.0f - 0.5f) * 0.02f;        // +- 1 cm
    }

    std::vector<LightmapUnwrapGroup> groups = { mesh.Group() };
    const LightmapUnwrapResult result = UnwrapForLightmap(groups);
    ASSERT_TRUE(result.success);

    LightmapUnwrapSettings unmerged;
    unmerged.minChartArea = 0.0f;
    const uint32_t before = UnwrapForLightmap(groups, unmerged).chartCount;
    EXPECT_LT(result.chartCount * 4u, before) << "20000 triangles of cloth: " << before << " charts, " << result.chartCount << " once the small ones are merged";
}

TEST(LightmapUnwrap, ASphereIsCutIntoSeveralNonOverlappingCharts) {
    const SoupMesh sphere = MakeSphere(24, 32, 1.0f);
    const LightmapUnwrapResult result = UnwrapForLightmap({ sphere.Group() });

    ExpectValidLayout(sphere, result);
    EXPECT_GT(result.chartCount, 3u);
    EXPECT_GT(result.groups[0].sourceVertex.size(), sphere.positions.size()) << "the seams split vertices";
    EXPECT_EQ(MaxCoverage(result.groups, 1024), 1);

    // A smaller maximum angle cuts more
    LightmapUnwrapSettings strict;
    strict.maxChartAngleDegrees = 20.0f;
    strict.maxSeedAngleDegrees = 30.0f;
    EXPECT_GT(UnwrapForLightmap({ sphere.Group() }, strict).chartCount, result.chartCount);
}

TEST(LightmapUnwrap, GroupsShareOneLayoutWithoutOverlap) {
    const SoupMesh a = MakeSharedCube(2.0f);
    const SoupMesh b = MakeGrid(4, 3.0f);
    const LightmapUnwrapResult result = UnwrapForLightmap({ a.Group(), b.Group() });

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.groups.size(), size_t(2));
    EXPECT_EQ(result.groups[0].indices.size(), a.indices.size());
    EXPECT_EQ(result.groups[1].indices.size(), b.indices.size());
    EXPECT_EQ(MaxCoverage(result.groups, 512), 1);
}

TEST(LightmapUnwrap, IsDeterministic) {
    const SoupMesh sphere = MakeSphere(16, 24, 2.0f);
    const LightmapUnwrapResult first = UnwrapForLightmap({ sphere.Group() });
    const LightmapUnwrapResult second = UnwrapForLightmap({ sphere.Group() });

    ASSERT_EQ(first.groups[0].uv.size(), second.groups[0].uv.size());
    for (size_t i = 0; i < first.groups[0].uv.size(); i++)
    {
        EXPECT_EQ(first.groups[0].uv[i], second.groups[0].uv[i]);
        EXPECT_EQ(first.groups[0].sourceVertex[i], second.groups[0].sourceVertex[i]);
    }
    EXPECT_EQ(first.groups[0].indices, second.groups[0].indices);
}

TEST(LightmapUnwrap, HandlesNothingToUnwrap) {
    EXPECT_FALSE(UnwrapForLightmap({}).success);

    SoupMesh degenerate;
    degenerate.positions = { glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f) };
    degenerate.normals = degenerate.positions;
    degenerate.indices = { 0, 1, 2 };
    EXPECT_FALSE(UnwrapForLightmap({ degenerate.Group() }).success) << "a triangle without an area has nothing to light";

    LightmapUnwrapGroup empty;
    EXPECT_FALSE(UnwrapForLightmap({ empty }).success);
}

TEST(LightmapUnwrap, ToleratesATriangleWithoutAreaBetweenRealOnes) {
    SoupMesh mesh = MakeGrid(3, 3.0f);
    // A sliver : two of its corners are the same point
    mesh.indices.insert(mesh.indices.end(), { 0, 0, 1 });

    const LightmapUnwrapResult result = UnwrapForLightmap({ mesh.Group() });
    ExpectValidLayout(mesh, result);
    EXPECT_EQ(result.chartCount, 1u);
}

TEST(LightmapUnwrap, ManyTinyChartsStillFit) {
    // Every triangle its own chart (all the normals differ a lot) : the padding has to give way, not the layout
    SoupMesh mesh;
    for (int i = 0; i < 3000; i++)
    {
        const float a = float(i) * 2.399963f;       // the golden angle : the facing spreads over the sphere
        const float y = 1.0f - 2.0f * (float(i) + 0.5f) / 3000.0f;
        const float r = std::sqrt(1.0f - y * y);
        const glm::vec3 n(std::cos(a) * r, y, std::sin(a) * r);
        const glm::vec3 t = glm::normalize(glm::cross(n, std::abs(n.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
        const glm::vec3 b = glm::cross(n, t);
        const glm::vec3 centre = n * 5.0f + glm::vec3(float(i) * 0.01f);
        const uint32_t base = uint32_t(mesh.positions.size());
        mesh.positions.insert(mesh.positions.end(), { centre, centre + t * 0.1f, centre + b * 0.1f });
        mesh.normals.insert(mesh.normals.end(), 3, n);
        mesh.indices.insert(mesh.indices.end(), { base, base + 1, base + 2 });
    }

    const LightmapUnwrapResult result = UnwrapForLightmap({ mesh.Group() });
    ExpectValidLayout(mesh, result);
    EXPECT_EQ(result.chartCount, 3000u);
    EXPECT_EQ(MaxCoverage(result.groups, 2048), 1);
}

// ---- The cache ----

namespace {

    // Turns the cache on in a folder of its own, off again afterwards (the setting is global)
    class UnwrapCache : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            directory = std::filesystem::temp_directory_path() / "shard_lightmap_uv_cache_test";
            std::filesystem::remove_all(directory);
            SetLightmapUnwrapCacheDirectory(directory.generic_string());
        }
        void TearDown() override
        {
            SetLightmapUnwrapCacheDirectory("");
            std::filesystem::remove_all(directory);
        }

        size_t FileCount() const
        {
            size_t count = 0;
            if (std::filesystem::exists(directory))
                for (const auto& entry : std::filesystem::directory_iterator(directory))
                    if (entry.path().extension() == ".luv")
                        count++;
            return count;
        }

        std::filesystem::path directory;
    };
}

TEST_F(UnwrapCache, TheSecondUnwrapOfAMeshComesFromTheCacheAndIsIdentical) {
    const SoupMesh sphere = MakeSphere(20, 28, 1.5f);

    const LightmapUnwrapResult first = UnwrapForLightmap({ sphere.Group() });
    EXPECT_FALSE(first.fromCache);
    EXPECT_EQ(FileCount(), size_t(1));

    const LightmapUnwrapResult second = UnwrapForLightmap({ sphere.Group() });
    EXPECT_TRUE(second.fromCache);
    EXPECT_EQ(FileCount(), size_t(1)) << "the same mesh does not make a second file";

    ASSERT_EQ(first.groups[0].uv.size(), second.groups[0].uv.size());
    for (size_t i = 0; i < first.groups[0].uv.size(); i++)
        EXPECT_EQ(first.groups[0].uv[i], second.groups[0].uv[i]);
    EXPECT_EQ(first.groups[0].sourceVertex, second.groups[0].sourceVertex);
    EXPECT_EQ(first.groups[0].indices, second.groups[0].indices);
    EXPECT_EQ(first.chartCount, second.chartCount);
    EXPECT_FLOAT_EQ(first.uvArea, second.uvArea);
    EXPECT_FLOAT_EQ(first.surfaceArea, second.surfaceArea);
}

TEST_F(UnwrapCache, AnotherMeshOrOtherSettingsAreNotServedFromIt) {
    SoupMesh sphere = MakeSphere(12, 16, 1.0f);
    EXPECT_FALSE(UnwrapForLightmap({ sphere.Group() }).fromCache);

    // One vertex moved
    sphere.positions[5] += glm::vec3(0.01f, 0.0f, 0.0f);
    EXPECT_FALSE(UnwrapForLightmap({ sphere.Group() }).fromCache);

    LightmapUnwrapSettings other;
    other.maxChartAngleDegrees = 25.0f;
    EXPECT_FALSE(UnwrapForLightmap({ sphere.Group() }, other).fromCache);

    EXPECT_EQ(FileCount(), size_t(3));
}

TEST_F(UnwrapCache, ADamagedFileIsIgnoredAndReplaced) {
    const SoupMesh cube = MakeSharedCube(2.0f);
    ASSERT_FALSE(UnwrapForLightmap({ cube.Group() }).fromCache);

    std::filesystem::path file;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.path().extension() == ".luv")
            file = entry.path();
    ASSERT_FALSE(file.empty());

    // Truncated
    {
        std::vector<char> bytes;
        {
            std::ifstream in(file, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), std::streamsize(bytes.size() / 2));
    }

    const LightmapUnwrapResult recomputed = UnwrapForLightmap({ cube.Group() });
    EXPECT_FALSE(recomputed.fromCache);
    EXPECT_TRUE(recomputed.success);
    EXPECT_EQ(recomputed.chartCount, 6u);

    // And the damaged file was replaced by a good one
    EXPECT_TRUE(UnwrapForLightmap({ cube.Group() }).fromCache);
}

TEST(LightmapUnwrapNoCache, NothingIsWrittenWithoutADirectory) {
    SetLightmapUnwrapCacheDirectory("");
    const SoupMesh grid = MakeGrid(4, 2.0f);
    EXPECT_FALSE(UnwrapForLightmap({ grid.Group() }).fromCache);
    EXPECT_FALSE(UnwrapForLightmap({ grid.Group() }).fromCache);
}

// ---- The meshes of the sample project ----

TEST(LightmapUnwrap, LoadedMeshesCarryTheirLightmapCoordinates) {
    for (const char* name : { "CornellBox.fbx", "Dragon.fbx", "cube.fbx" })
    {
        const auto path = FindModel(name);
        if (!path)
            GTEST_SKIP() << "the sample project's models are not next to the tests";

        const MeshCPUData data = DecodeMeshFile(Shard::Engine::Filesystem::Path(*path));
        ASSERT_TRUE(data.success) << name;

        const VertexElement* element = nullptr;
        for (const VertexElement& e : data.layout.GetElements())
            if (e.name == "aLightmapUV")
                element = &e;
        ASSERT_NE(element, nullptr) << name << " has no aLightmapUV attribute";
        EXPECT_EQ(element->location, 5u);

        const size_t stride = data.layout.GetStride();
        const size_t vertexCount = data.vertices.size() / stride;
        std::vector<LightmapUnwrapGroupResult> asGroups(1);
        asGroups[0].indices = data.indices;
        for (size_t v = 0; v < vertexCount; v++)
        {
            glm::vec2 uv;
            std::memcpy(&uv, data.vertices.data() + v * stride + element->offset, sizeof(glm::vec2));
            EXPECT_GE(uv.x, 0.0f);
            EXPECT_LE(uv.x, 1.0f);
            EXPECT_GE(uv.y, 0.0f);
            EXPECT_LE(uv.y, 1.0f);
            asGroups[0].uv.push_back(uv);
        }

        // Every triangle of every material slot is somewhere of its own in the one square
        EXPECT_EQ(MaxCoverage(asGroups, 1024), 1) << name;
    }
}

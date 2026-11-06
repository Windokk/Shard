#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <string>

#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/frustum.hpp"

#include <glm/gtc/matrix_transform.hpp>

using namespace Shard::Engine::Rendering;

namespace {

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

    glm::vec3 PositionOf(const MeshCPUData& data, uint32_t vertexIndex)
    {
        size_t stride = data.layout.GetStride();
        size_t offset = 0;
        for (const auto& element : data.layout.GetElements())
            if (element.name == "aPos")
                offset = element.offset;

        glm::vec3 position;
        std::memcpy(&position, data.vertices.data() + vertexIndex * stride + offset, sizeof(glm::vec3));
        return position;
    }

    void ExpectConsistentSubmeshes(const MeshCPUData& data)
    {
        ASSERT_TRUE(data.success);
        ASSERT_FALSE(data.submeshes.empty());

        const size_t vertexCount = data.vertices.size() / data.layout.GetStride();

        std::vector<SubMesh> sorted = data.submeshes;
        std::sort(sorted.begin(), sorted.end(), [](const SubMesh& a, const SubMesh& b) { return a.indexOffset < b.indexOffset; });

        size_t expectedOffset = 0;
        for (const SubMesh& submesh : sorted)
        {
            EXPECT_EQ(submesh.indexOffset, expectedOffset);
            EXPECT_EQ(submesh.indexCount % 3, 0u);
            EXPECT_LE(submesh.indexCount, kMaxSubMeshTriangles * 3);
            expectedOffset += submesh.indexCount;

            const float eps = 1e-3f;
            for (size_t i = submesh.indexOffset; i < submesh.indexOffset + submesh.indexCount; i++)
            {
                ASSERT_LT(data.indices[i], vertexCount);

                glm::vec3 position = PositionOf(data, data.indices[i]);
                EXPECT_TRUE(glm::all(glm::greaterThanEqual(position, submesh.boundsMin - eps)));
                EXPECT_TRUE(glm::all(glm::lessThanEqual(position, submesh.boundsMax + eps)));
            }
        }
        EXPECT_EQ(expectedOffset, data.indices.size());
    }

}

TEST(MeshChunking, SmallMeshOneSubmeshPerSlot)
{
    auto path = FindModel("Dragon.fbx");
    if (!path)
        GTEST_SKIP() << "sample project not found";

    MeshCPUData data = DecodeMeshFile(Shard::Engine::Filesystem::Path(*path));
    ExpectConsistentSubmeshes(data);

    std::set<uint32_t> slots;
    for (const SubMesh& submesh : data.submeshes)
        slots.insert(submesh.materialIndex);

    EXPECT_EQ(slots.size(), data.submeshes.size());
}

// Sponza is ~5.7M triangles, too slow for Debug. Opt-in: --gtest_also_run_disabled_tests --gtest_filter=*LargeMeshSplits
TEST(MeshChunking, DISABLED_LargeMeshSplits)
{
    auto path = FindModel("Sponza.fbx");
    if (!path)
        GTEST_SKIP() << "sample project not found";

    MeshCPUData data = DecodeMeshFile(Shard::Engine::Filesystem::Path(*path));
    ExpectConsistentSubmeshes(data);

    std::set<uint32_t> slots;
    for (const SubMesh& submesh : data.submeshes)
        slots.insert(submesh.materialIndex);

    EXPECT_GT(data.submeshes.size(), slots.size());

    const glm::vec3 meshSize = data.boundsMax - data.boundsMin;
    size_t smallChunks = 0;
    for (const SubMesh& submesh : data.submeshes)
        if (submesh.indexCount > 0 && glm::length(submesh.boundsMax - submesh.boundsMin) < glm::length(meshSize) * 0.5f)
            smallChunks++;
    EXPECT_GT(smallChunks, data.submeshes.size() / 2);

    size_t totalTriangles = data.indices.size() / 3;
    std::cout << "[ info ] " << data.submeshes.size() << " submeshes over " << slots.size() << " material slots, "
              << totalTriangles << " triangles, mesh size " << meshSize.x << " x " << meshSize.y << " x " << meshSize.z << std::endl;

    const glm::vec3 eye(0.0f, 2.0f, 0.0f);
    for (glm::vec3 direction : { glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1) })
    {
        glm::mat4 vp = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f) * glm::lookAt(eye, eye + direction, glm::vec3(0, 1, 0));
        FrustumPlanes planes = ExtractFrustumPlanes(vp);

        size_t visibleTriangles = 0;
        for (const SubMesh& submesh : data.submeshes)
            if (AABBInFrustum(planes, submesh.boundsMin, submesh.boundsMax))
                visibleTriangles += submesh.indexCount / 3;

        std::cout << "[ info ] looking along (" << direction.x << ", " << direction.y << ", " << direction.z << ") : "
                  << visibleTriangles << " triangles drawn (" << (100 * visibleTriangles / std::max<size_t>(totalTriangles, 1)) << "%)" << std::endl;
    }
}

TEST(Frustum, OrthoBoxCulls)
{
    glm::mat4 vp = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 0.0f, 20.0f) * glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0), glm::vec3(0, 1, 0));
    FrustumPlanes planes = ExtractFrustumPlanes(vp);

    EXPECT_TRUE(AABBInFrustum(planes, glm::vec3(-1), glm::vec3(1)));
    EXPECT_TRUE(AABBInFrustum(planes, glm::vec3(4, 4, -5), glm::vec3(8, 8, 5)));
    EXPECT_FALSE(AABBInFrustum(planes, glm::vec3(6, -1, -1), glm::vec3(8, 1, 1)));
    EXPECT_FALSE(AABBInFrustum(planes, glm::vec3(-1, 6, -1), glm::vec3(1, 8, 1)));
    EXPECT_FALSE(AABBInFrustum(planes, glm::vec3(-1, -1, 11), glm::vec3(1, 1, 12)));
    EXPECT_FALSE(AABBInFrustum(planes, glm::vec3(-1, -1, -11), glm::vec3(1, 1, -10.5f)));
}

TEST(Frustum, PerspectiveMatchesCamera)
{
    glm::mat4 vp = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f) * glm::lookAt(glm::vec3(0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    FrustumPlanes planes = ExtractFrustumPlanes(vp);

    EXPECT_TRUE(AABBInFrustum(planes, glm::vec3(-1, -1, -10), glm::vec3(1, 1, -8)));
    EXPECT_FALSE(AABBInFrustum(planes, glm::vec3(-1, -1, 8), glm::vec3(1, 1, 10)));
    EXPECT_FALSE(AABBInFrustum(planes, glm::vec3(50, -1, -10), glm::vec3(52, 1, -8)));
    EXPECT_FALSE(AABBInFrustum(planes, glm::vec3(-1, -1, -200), glm::vec3(1, 1, -150)));
}

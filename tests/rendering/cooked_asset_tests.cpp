#include <cstring>
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/config/cvar.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh_cooked.hpp"
#include "engine/renderer/rhi/resources/texture/image_export.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Rendering;

namespace {
    namespace fs = std::filesystem;

    class CookedAssetTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            m_Dir = fs::temp_directory_path() / ("shard_cooked_asset_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
            std::error_code ec;
            fs::remove_all(m_Dir, ec);
            fs::create_directories(m_Dir);
            TextureCache::SetDirectory("");
            Core::CVarRegistry::Global().Execute("texture.compress true");
        }

        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all(m_Dir, ec);
        }

        std::string Write(const std::string& name, const std::vector<uint8_t>& bytes)
        {
            const fs::path file = m_Dir / name;
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            return file.generic_string();
        }

        Filesystem::Path WritePng(const std::string& name, uint32_t size)
        {
            std::vector<uint8_t> pixels(size_t(size) * size * 4);
            for (uint32_t y = 0; y < size; y++)
                for (uint32_t x = 0; x < size; x++) {
                    uint8_t* p = &pixels[(size_t(y) * size + x) * 4];
                    p[0] = uint8_t(x * 4);
                    p[1] = uint8_t(y * 4);
                    p[2] = uint8_t((x + y) * 2);
                    p[3] = 255;
                }
            const Filesystem::Path path((m_Dir / name).generic_string());
            EXPECT_TRUE(ImageExport::WritePNG(path, size, size, 4, pixels.data()));
            return path;
        }

        fs::path m_Dir;
    };

    MeshCPUData SampleMesh()
    {
        MeshCPUData mesh;
        mesh.layout = StandardMeshLayout();
        mesh.vertices.resize(size_t(mesh.layout.GetStride()) * 4);
        for (size_t i = 0; i < mesh.vertices.size(); ++i) mesh.vertices[i] = uint8_t(i * 7 + 3);
        mesh.indices = { 0, 1, 2, 2, 1, 3 };
        SubMesh a;
        a.indexOffset = 0;
        a.indexCount = 3;
        a.vertexCount = 3;
        a.materialIndex = 1;
        a.boundsMin = glm::vec3(-1, -2, -3);
        a.boundsMax = glm::vec3(1, 2, 3);
        SubMesh b = a;
        b.indexOffset = 3;
        b.materialIndex = 0;
        mesh.submeshes = { a, b };
        mesh.boundsMin = glm::vec3(-1, -2, -3);
        mesh.boundsMax = glm::vec3(1, 2, 3);
        mesh.success = true;
        return mesh;
    }
}

TEST_F(CookedAssetTest, MeshRoundTrips)
{
    const MeshCPUData in = SampleMesh();
    const std::vector<uint8_t> bytes = SerializeMeshData(in);
    MeshCPUData out;
    ASSERT_TRUE(DeserializeMeshData(bytes.data(), bytes.size(), out));
    EXPECT_TRUE(out.success);
    EXPECT_EQ(out.vertices, in.vertices);
    EXPECT_EQ(out.indices, in.indices);
    ASSERT_EQ(out.submeshes.size(), 2u);
    EXPECT_EQ(out.submeshes[0].materialIndex, 1u);
    EXPECT_EQ(out.submeshes[1].indexOffset, 3u);
    EXPECT_EQ(out.submeshes[0].boundsMax, in.submeshes[0].boundsMax);
    EXPECT_EQ(out.boundsMin, in.boundsMin);
    EXPECT_EQ(out.layout.GetStride(), in.layout.GetStride());
    EXPECT_EQ(out.layout.GetElements().size(), 6u);
}

TEST_F(CookedAssetTest, MeshRefusesDamagedData)
{
    const std::vector<uint8_t> bytes = SerializeMeshData(SampleMesh());
    MeshCPUData out;
    for (size_t cut : { size_t(0), size_t(3), size_t(20), bytes.size() - 1 })
        EXPECT_FALSE(DeserializeMeshData(bytes.data(), cut, out)) << "cut at " << cut;

    std::vector<uint8_t> badMagic = bytes;
    badMagic[0] = 'X';
    EXPECT_FALSE(DeserializeMeshData(badMagic.data(), badMagic.size(), out));

    // An index that points past the vertices must not reach the GPU
    MeshCPUData wild = SampleMesh();
    wild.indices[1] = 99;
    const std::vector<uint8_t> wildBytes = SerializeMeshData(wild);
    EXPECT_FALSE(DeserializeMeshData(wildBytes.data(), wildBytes.size(), out));

    // A submesh outside the index buffer too
    MeshCPUData far = SampleMesh();
    far.submeshes[1].indexCount = 100;
    const std::vector<uint8_t> farBytes = SerializeMeshData(far);
    EXPECT_FALSE(DeserializeMeshData(farBytes.data(), farBytes.size(), out));

    std::vector<uint8_t> manySubmeshes = bytes;
    const uint32_t huge = 0xFFFFFFFFu;
    std::memcpy(manySubmeshes.data() + 28, &huge, sizeof(huge));
    EXPECT_FALSE(DeserializeMeshData(manySubmeshes.data(), manySubmeshes.size(), out));
}

TEST_F(CookedAssetTest, MeshLoadsNextToItsSource)
{
    const std::string source = (m_Dir / "thing.fbx").generic_string();
    MeshCPUData none;
    EXPECT_FALSE(LoadCookedMesh(source, none));
    Write("thing.fbx.smesh", SerializeMeshData(SampleMesh()));
    MeshCPUData loaded;
    ASSERT_TRUE(LoadCookedMesh(source, loaded));
    EXPECT_EQ(loaded.indices.size(), 6u);
}

TEST_F(CookedAssetTest, CookedTextureIsUsedInsteadOfTheSource)
{
    const Filesystem::Path png = WritePng("rock.png", 256);
    std::vector<uint8_t> cooked;
    ASSERT_TRUE(CookTexture(png, true, 1, cooked));
    EXPECT_FALSE(cooked.empty());

    // The placeholder left in the pak is empty : only the cooked file can give the texture
    Write("rock.png.srgb.stx", cooked);
    std::ofstream(png.full, std::ios::binary | std::ios::trunc).close();
    const TextureDecodeResult result = DecodeTextureFileForUpload(png, 1, true);
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.compressed);
    EXPECT_EQ(result.width, 256);
    EXPECT_EQ(result.compressed->width, 256u);
    EXPECT_GT(result.compressed->mips.size(), 1u);

    // Asked as data, the colour variant still serves rather than failing
    const TextureDecodeResult asData = DecodeTextureFileForUpload(png, 1, false);
    EXPECT_TRUE(asData.success);
}

TEST_F(CookedAssetTest, TooSmallATextureIsNotCooked)
{
    const Filesystem::Path png = WritePng("tiny.png", 2);
    std::vector<uint8_t> cooked;
    EXPECT_FALSE(CookTexture(png, false, 1, cooked));
    EXPECT_FALSE(CookTexture(Filesystem::Path((m_Dir / "missing.png").generic_string()), false, 1, cooked));
}

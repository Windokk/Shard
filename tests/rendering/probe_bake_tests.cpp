#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "engine/rendering/lighting/probe_bake.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Rendering;

namespace {

    // 2x1x2 probes with 4x4 tiles : 4 probes -> 2 per atlas row, (4 + 2 border) texels per tile -> a
    // 12x12 atlas.
    ProbeBakeData MakeBake()
    {
        ProbeBakeData data;
        data.probeCounts = glm::ivec3(2, 1, 2);
        data.tileSize = 4;
        data.atlasProbesPerRow = 2;
        data.atlasSize = 12;
        data.probeCount = 4;
        data.gridOrigin = glm::vec3(-1.5f, 0.25f, 3.0f);
        data.gridSpacing = glm::vec3(1.0f, 2.0f, 0.5f);

        data.irradiance.resize((size_t)data.atlasSize * data.atlasSize * 4);
        data.distance.resize((size_t)data.atlasSize * data.atlasSize * 2);

        for (size_t i = 0; i < data.irradiance.size(); i++)
            data.irradiance[i] = (uint16_t)(i * 2654435761u >> 8);
        data.irradiance[0] = 0x0A0D;
        data.irradiance[1] = 0x0D0A;

        for (size_t i = 0; i < data.distance.size(); i++)
            data.distance[i] = (uint16_t)(0xFFFF - i);

        data.probeState = {
            glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
            glm::vec4(0.1f, -0.2f, 0.3f, 1.0f),
            glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
            glm::vec4(-0.4f, 0.0f, 0.05f, 1.0f),
        };

        return data;
    }

    class ProbeBakeFile : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                dir = std::filesystem::temp_directory_path() / "shard_probe_bake_tests";
                std::filesystem::remove_all(dir);
                std::filesystem::create_directories(dir);
                path = Filesystem::Path((dir / "volume.probes").string());
            }

            void TearDown() override
            {
                std::error_code ec;
                std::filesystem::remove_all(dir, ec);
            }

            std::string ReadAll() const
            {
                std::ifstream in(path.full, std::ios::binary);
                return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            }

            void WriteAll(const std::string& bytes) const
            {
                std::ofstream out(path.full, std::ios::binary | std::ios::trunc);
                out.write(bytes.data(), (std::streamsize)bytes.size());
            }

            std::filesystem::path dir;
            Filesystem::Path path;
    };

}

TEST_F(ProbeBakeFile, RoundTripPreservesEverything) {
    ProbeBakeData original = MakeBake();
    ASSERT_TRUE(WriteProbeBakeFile(path, original));

    std::shared_ptr<ProbeBakeData> loaded = DecodeProbeBakeFile(path);
    ASSERT_NE(loaded, nullptr);

    EXPECT_EQ(loaded->probeCounts, original.probeCounts);
    EXPECT_EQ(loaded->tileSize, original.tileSize);
    EXPECT_EQ(loaded->atlasProbesPerRow, original.atlasProbesPerRow);
    EXPECT_EQ(loaded->atlasSize, original.atlasSize);
    EXPECT_EQ(loaded->probeCount, original.probeCount);
    EXPECT_EQ(loaded->gridOrigin, original.gridOrigin);
    EXPECT_EQ(loaded->gridSpacing, original.gridSpacing);

    // Byte-exact : this is the data the shader reads, and it must come back untouched.
    EXPECT_EQ(loaded->irradiance, original.irradiance);
    EXPECT_EQ(loaded->distance, original.distance);
    EXPECT_EQ(loaded->probeState, original.probeState);
}

TEST_F(ProbeBakeFile, WriteCreatesMissingDirectories) {
    Filesystem::Path nested((dir / "probes" / "nested" / "volume.probes").string());

    ASSERT_TRUE(WriteProbeBakeFile(nested, MakeBake()));
    EXPECT_NE(DecodeProbeBakeFile(nested), nullptr);
}

TEST_F(ProbeBakeFile, WriteRefusesDataThatContradictsItsOwnHeader) {
    ProbeBakeData bad = MakeBake();
    bad.irradiance.pop_back();

    EXPECT_FALSE(WriteProbeBakeFile(path, bad));
    EXPECT_FALSE(std::filesystem::exists(path.full));

    bad = MakeBake();
    bad.probeState.pop_back();
    EXPECT_FALSE(WriteProbeBakeFile(path, bad));
}

TEST_F(ProbeBakeFile, MissingFileIsRejected) {
    EXPECT_EQ(DecodeProbeBakeFile(path), nullptr);
}

TEST_F(ProbeBakeFile, TruncatedFileIsRejected) {
    ASSERT_TRUE(WriteProbeBakeFile(path, MakeBake()));
    std::string bytes = ReadAll();

    // Cut anywhere - inside the payload, and inside the header.
    for (size_t keep : { bytes.size() - 1, bytes.size() / 2, (size_t)40, (size_t)0 })
    {
        WriteAll(bytes.substr(0, keep));
        EXPECT_EQ(DecodeProbeBakeFile(path), nullptr) << "kept " << keep << " bytes";
    }
}

TEST_F(ProbeBakeFile, TrailingGarbageIsRejected) {
    ASSERT_TRUE(WriteProbeBakeFile(path, MakeBake()));
    WriteAll(ReadAll() + "extra");

    EXPECT_EQ(DecodeProbeBakeFile(path), nullptr);
}

TEST_F(ProbeBakeFile, WrongMagicIsRejected) {
    ASSERT_TRUE(WriteProbeBakeFile(path, MakeBake()));
    std::string bytes = ReadAll();
    bytes[0] = 'X';
    WriteAll(bytes);

    EXPECT_EQ(DecodeProbeBakeFile(path), nullptr);
}

TEST_F(ProbeBakeFile, UnknownVersionIsRejected) {
    ASSERT_TRUE(WriteProbeBakeFile(path, MakeBake()));
    std::string bytes = ReadAll();
    bytes[4] = 99; // version field, right after the 4-byte magic
    WriteAll(bytes);

    EXPECT_EQ(DecodeProbeBakeFile(path), nullptr);
}

TEST_F(ProbeBakeFile, InconsistentHeaderIsRejected) {
    ASSERT_TRUE(WriteProbeBakeFile(path, MakeBake()));
    std::string bytes = ReadAll();
    bytes[32] = 5;
    WriteAll(bytes);

    EXPECT_EQ(DecodeProbeBakeFile(path), nullptr);
}

TEST(ProbeBakeCompatibility, AcceptsTheGridItWasBakedOn) {
    ProbeBakeData bake = MakeBake();

    std::string why;
    EXPECT_TRUE(IsProbeBakeCompatible(bake, bake.probeCounts, bake.tileSize, bake.gridOrigin, bake.gridSpacing, &why));
    EXPECT_TRUE(why.empty());
}

TEST(ProbeBakeCompatibility, ToleratesFloatNoiseInThePlacement) {
    ProbeBakeData bake = MakeBake();

    // Re-deriving the origin from the actor's transform on load can differ in the last bits.
    EXPECT_TRUE(IsProbeBakeCompatible(bake, bake.probeCounts, bake.tileSize,
                                      bake.gridOrigin + glm::vec3(1e-5f), bake.gridSpacing - glm::vec3(1e-5f)));
}

TEST(ProbeBakeCompatibility, RejectsEachKindOfChange) {
    ProbeBakeData bake = MakeBake();
    std::string why;

    EXPECT_FALSE(IsProbeBakeCompatible(bake, glm::ivec3(2, 2, 2), bake.tileSize, bake.gridOrigin, bake.gridSpacing, &why));
    EXPECT_EQ(why, "probe counts changed");

    EXPECT_FALSE(IsProbeBakeCompatible(bake, bake.probeCounts, 8, bake.gridOrigin, bake.gridSpacing, &why));
    EXPECT_EQ(why, "rays per probe changed");

    EXPECT_FALSE(IsProbeBakeCompatible(bake, bake.probeCounts, bake.tileSize, bake.gridOrigin, bake.gridSpacing * 2.0f, &why));
    EXPECT_EQ(why, "volume was resized");

    // A moved volume : the baked light would land in the wrong part of the level.
    EXPECT_FALSE(IsProbeBakeCompatible(bake, bake.probeCounts, bake.tileSize, bake.gridOrigin + glm::vec3(0.0f, 1.0f, 0.0f), bake.gridSpacing, &why));
    EXPECT_EQ(why, "volume was moved");
}

TEST(ProbeBakeCompatibility, ReasonIsOptional) {
    ProbeBakeData bake = MakeBake();
    EXPECT_FALSE(IsProbeBakeCompatible(bake, glm::ivec3(9, 9, 9), bake.tileSize, bake.gridOrigin, bake.gridSpacing));
}

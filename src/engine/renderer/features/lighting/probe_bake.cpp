#include "probe_bake.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <type_traits>

#include "engine/debugging/logger.hpp"

namespace Shard::Engine::Rendering {

    namespace {

        constexpr char kMagic[4] = { 'S', 'H', 'P', 'B' };
        constexpr uint32_t kVersion = 1;

        // Sanity ceiling on atlasSize when reading : a corrupt header must not be able to make the
        // decoder try to allocate gigabytes. 16384 is the usual GL max texture size and already far
        // beyond any real volume (the default 8x4x8 grid is 160x160).
        constexpr uint32_t kMaxAtlasSize = 16384;

        // Followed on disk by the irradiance atlas, the distance atlas and the probe state, in that
        // order, each with a size fully determined by these fields (see ExpectedPayloadBytes).
        struct FileHeader
        {
            char magic[4];
            uint32_t version;
            int32_t probeCounts[3];
            uint32_t tileSize;
            uint32_t atlasProbesPerRow;
            uint32_t atlasSize;
            uint32_t probeCount;
            float gridOrigin[3];
            float gridSpacing[3];
        };
        static_assert(std::is_trivially_copyable_v<FileHeader>, "FileHeader is written to disk as raw bytes");

        uint64_t IrradianceBytes(uint32_t atlasSize) { return (uint64_t)atlasSize * atlasSize * 4 * sizeof(uint16_t); }
        uint64_t DistanceBytes(uint32_t atlasSize)   { return (uint64_t)atlasSize * atlasSize * 2 * sizeof(uint16_t); }
        uint64_t StateBytes(uint32_t probeCount)     { return (uint64_t)probeCount * sizeof(glm::vec4); }

        bool NearlyEqual(const glm::vec3& a, const glm::vec3& b)
        {
            // 1 mm : far below anything visible, comfortably above the float noise of re-deriving the
            // origin from the actor's transform on the next load.
            return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(1e-3f)));
        }
    }

    bool WriteProbeBakeFile(const Filesystem::Path& path, const ProbeBakeData& data)
    {
        if (data.atlasSize == 0 || data.probeCount == 0 ||
            data.irradiance.size() * sizeof(uint16_t) != IrradianceBytes(data.atlasSize) ||
            data.distance.size() * sizeof(uint16_t) != DistanceBytes(data.atlasSize) ||
            data.probeState.size() != data.probeCount)
        {
            DEBUG_ERROR("Probe bake : refusing to write '" + path.full + "', its data doesn't match its own header.");
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path.full).parent_path(), ec);

        // Binary explicitly : Filesystem::Path::WriteFile opens in text mode, which on Windows would
        // rewrite every 0x0A byte in the payload as CRLF.
        std::ofstream out(path.full, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            DEBUG_ERROR("Probe bake : couldn't open '" + path.full + "' for writing.");
            return false;
        }

        FileHeader header{};
        std::memcpy(header.magic, kMagic, sizeof(kMagic));
        header.version = kVersion;
        header.probeCounts[0] = data.probeCounts.x;
        header.probeCounts[1] = data.probeCounts.y;
        header.probeCounts[2] = data.probeCounts.z;
        header.tileSize = data.tileSize;
        header.atlasProbesPerRow = data.atlasProbesPerRow;
        header.atlasSize = data.atlasSize;
        header.probeCount = data.probeCount;
        header.gridOrigin[0] = data.gridOrigin.x;
        header.gridOrigin[1] = data.gridOrigin.y;
        header.gridOrigin[2] = data.gridOrigin.z;
        header.gridSpacing[0] = data.gridSpacing.x;
        header.gridSpacing[1] = data.gridSpacing.y;
        header.gridSpacing[2] = data.gridSpacing.z;

        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(reinterpret_cast<const char*>(data.irradiance.data()), (std::streamsize)IrradianceBytes(data.atlasSize));
        out.write(reinterpret_cast<const char*>(data.distance.data()), (std::streamsize)DistanceBytes(data.atlasSize));
        out.write(reinterpret_cast<const char*>(data.probeState.data()), (std::streamsize)StateBytes(data.probeCount));
        out.flush();

        if (!out.good())
        {
            DEBUG_ERROR("Probe bake : error while writing '" + path.full + "'.");
            return false;
        }

        return true;
    }

    std::shared_ptr<ProbeBakeData> DecodeProbeBakeFile(const Filesystem::Path& path)
    {
        std::ifstream in(path.full, std::ios::binary | std::ios::ate);
        if (!in.is_open())
        {
            DEBUG_ERROR("Probe bake : couldn't open '" + path.full + "'.");
            return nullptr;
        }

        const uint64_t fileSize = (uint64_t)in.tellg();
        in.seekg(0, std::ios::beg);

        FileHeader header{};
        if (fileSize < sizeof(header) || !in.read(reinterpret_cast<char*>(&header), sizeof(header)))
        {
            DEBUG_ERROR("Probe bake : '" + path.full + "' is too small to be a probe bake.");
            return nullptr;
        }

        if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0)
        {
            DEBUG_ERROR("Probe bake : '" + path.full + "' is not a probe bake file.");
            return nullptr;
        }

        if (header.version != kVersion)
        {
            DEBUG_ERROR("Probe bake : '" + path.full + "' is version " + std::to_string(header.version) +
                        ", this build reads version " + std::to_string(kVersion) + " - re-bake the volume.");
            return nullptr;
        }

        // Everything below is derived from the header, so a header that contradicts itself (or the
        // file's real size) means a truncated or corrupt file. Checked before any allocation.
        const uint64_t expectedProbes = (uint64_t)std::max(header.probeCounts[0], 0) *
                                        (uint64_t)std::max(header.probeCounts[1], 0) *
                                        (uint64_t)std::max(header.probeCounts[2], 0);

        const bool layoutOk =
            header.tileSize > 0 &&
            header.atlasProbesPerRow > 0 &&
            header.atlasSize > 0 && header.atlasSize <= kMaxAtlasSize &&
            header.probeCount > 0 && header.probeCount == expectedProbes &&
            (uint64_t)header.atlasProbesPerRow * header.atlasProbesPerRow >= header.probeCount &&
            (uint64_t)header.atlasProbesPerRow * (header.tileSize + 2) == header.atlasSize;

        if (!layoutOk)
        {
            DEBUG_ERROR("Probe bake : '" + path.full + "' has an inconsistent header.");
            return nullptr;
        }

        const uint64_t expectedSize = sizeof(header) + IrradianceBytes(header.atlasSize) +
                                      DistanceBytes(header.atlasSize) + StateBytes(header.probeCount);
        if (fileSize != expectedSize)
        {
            DEBUG_ERROR("Probe bake : '" + path.full + "' is " + std::to_string(fileSize) + " bytes, expected " +
                        std::to_string(expectedSize) + " (truncated or corrupt).");
            return nullptr;
        }

        auto data = std::make_shared<ProbeBakeData>();
        data->probeCounts = glm::ivec3(header.probeCounts[0], header.probeCounts[1], header.probeCounts[2]);
        data->tileSize = header.tileSize;
        data->atlasProbesPerRow = header.atlasProbesPerRow;
        data->atlasSize = header.atlasSize;
        data->probeCount = header.probeCount;
        data->gridOrigin = glm::vec3(header.gridOrigin[0], header.gridOrigin[1], header.gridOrigin[2]);
        data->gridSpacing = glm::vec3(header.gridSpacing[0], header.gridSpacing[1], header.gridSpacing[2]);

        data->irradiance.resize(IrradianceBytes(header.atlasSize) / sizeof(uint16_t));
        data->distance.resize(DistanceBytes(header.atlasSize) / sizeof(uint16_t));
        data->probeState.resize(header.probeCount);

        in.read(reinterpret_cast<char*>(data->irradiance.data()), (std::streamsize)IrradianceBytes(header.atlasSize));
        in.read(reinterpret_cast<char*>(data->distance.data()), (std::streamsize)DistanceBytes(header.atlasSize));
        in.read(reinterpret_cast<char*>(data->probeState.data()), (std::streamsize)StateBytes(header.probeCount));

        if (!in)
        {
            DEBUG_ERROR("Probe bake : read error on '" + path.full + "'.");
            return nullptr;
        }

        return data;
    }

    bool IsProbeBakeCompatible(const ProbeBakeData& data, const glm::ivec3& probeCounts, uint32_t tileSize,
                               const glm::vec3& gridOrigin, const glm::vec3& gridSpacing, std::string* whyNot)
    {
        auto fail = [&](const char* reason)
        {
            if (whyNot)
                *whyNot = reason;
            return false;
        };

        if (data.probeCounts != probeCounts)
            return fail("probe counts changed");

        if (data.tileSize != tileSize)
            return fail("rays per probe changed");

        if (!NearlyEqual(data.gridSpacing, gridSpacing))
            return fail("volume was resized");

        if (!NearlyEqual(data.gridOrigin, gridOrigin))
            return fail("volume was moved");

        return true;
    }
}

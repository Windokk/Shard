#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Rendering {

    constexpr const char* kProbeBakeExtension = ".probes";

    struct ProbeBakeData
    {
        glm::ivec3 probeCounts = glm::ivec3(0);
        uint32_t tileSize = 0;
        uint32_t atlasProbesPerRow = 0;
        uint32_t atlasSize = 0;
        uint32_t probeCount = 0;
        glm::vec3 gridOrigin = glm::vec3(0.0f);
        glm::vec3 gridSpacing = glm::vec3(1.0f);

        std::vector<uint16_t> irradiance;
        std::vector<uint16_t> distance;

        std::vector<glm::vec4> probeState;
    };

    // Little-endian on every target (x86-64, ARM64), so raw structs are written.
    bool WriteProbeBakeFile(const Filesystem::Path& path, const ProbeBakeData& data);

    std::shared_ptr<ProbeBakeData> DecodeProbeBakeFile(const Filesystem::Path& path);

    bool IsProbeBakeCompatible(const ProbeBakeData& data, const glm::ivec3& probeCounts, uint32_t tileSize,
                               const glm::vec3& gridOrigin, const glm::vec3& gridSpacing, std::string* whyNot = nullptr);
}

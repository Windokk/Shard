#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/filesystem/filesystem.hpp"

namespace Shard::Engine::Rendering {

    // Extension of a baked probe volume file (registered in the asset database like any other project
    // resource - see ProbeVolume::OnBakeFinished).
    constexpr const char* kProbeBakeExtension = ".probes";

    // CPU-side image of one baked ProbeVolume : everything ProbeManager needs to bring a volume up
    // already converged instead of tracing it from zero (see ProbeManager::AddActiveVolume). Holds the
    // exact bytes the GPU holds - the published atlases are RGBA16F / RG16F, and the pixels here are
    // those same packed half floats - so loading is a file read plus one glTexSubImage2D per atlas, with
    // no per-texel conversion on either the bake or the load side.
    //
    // The header fields describe the grid the data was traced on. They are what lets a load reject a
    // file that no longer matches its volume (resized, re-counted, moved, different ray count) instead
    // of uploading atlases whose layout the shader would read wrongly - see IsProbeBakeCompatible().
    struct ProbeBakeData
    {
        glm::ivec3 probeCounts = glm::ivec3(0);
        uint32_t tileSize = 0;
        uint32_t atlasProbesPerRow = 0;
        uint32_t atlasSize = 0;
        uint32_t probeCount = 0;
        glm::vec3 gridOrigin = glm::vec3(0.0f);
        glm::vec3 gridSpacing = glm::vec3(1.0f);

        // atlasSize * atlasSize texels, 4 halves (RGBA16F) per irradiance texel and 2 (RG16F) per
        // distance texel - see ProbeManager::VolumeSlot for what the two atlases hold.
        std::vector<uint16_t> irradiance;
        std::vector<uint16_t> distance;

        // One vec4 per probe : .w classification flag, .xyz relocation offset (see
        // VolumeSlot::probeStateBuffer). Without it the shader would treat every probe as active and
        // unrelocated, undoing what the bake worked out about probes stuck inside geometry.
        std::vector<glm::vec4> probeState;
    };

    // Writes `data` to `path` (creating its parent directory). Returns false on any I/O failure. The
    // file is little-endian on every platform the engine targets (x86-64 / ARM64), so it is written as
    // raw structs rather than byte-swapped field by field.
    bool WriteProbeBakeFile(const Filesystem::Path& path, const ProbeBakeData& data);

    // Pure CPU read + validation of a bake file - no GL calls, so it is safe on a worker thread (the
    // level prefetcher decodes it there alongside the textures). Returns null if the file is missing,
    // truncated, from another format version, or internally inconsistent.
    std::shared_ptr<ProbeBakeData> DecodeProbeBakeFile(const Filesystem::Path& path);

    // Whether `data` was traced on exactly this grid. probeCounts and tileSize (which follows from the
    // volume's raysPerProbe) must match exactly, since they fix the atlas layout; the origin/spacing
    // are compared with a small tolerance, because they follow the actor's world position and a moved
    // volume would otherwise place baked light in the wrong part of the level. On mismatch, `whyNot`
    // (if given) receives a short reason for the log.
    bool IsProbeBakeCompatible(const ProbeBakeData& data, const glm::ivec3& probeCounts, uint32_t tileSize,
                               const glm::vec3& gridOrigin, const glm::vec3& gridSpacing, std::string* whyNot = nullptr);
}

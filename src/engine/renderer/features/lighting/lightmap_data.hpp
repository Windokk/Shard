#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Rendering {

    struct LightmapSettings
    {
        float texelsPerMeter = 32.0f;

        uint32_t minResolution = 16;
        uint32_t maxResolution = 1024;

        uint32_t maxPageSize = 2048;

        uint32_t samples = 256;
        uint32_t maxBounces = 5;

        float indirectIntensity = 1.0f;

        bool denoise = true;

        uint32_t denoiseIterations = 5;
        float denoiseLuminanceSigma = 4.0f;
        float denoiseNormalPower = 48.0f;
        float denoisePlaneSigma = 2.0f;

        // Clamp the luminance of a path. A rare bright one is a white speck no sample count removes. 0 disables.
        float fireflyClamp = 32.0f;

        bool operator==(const LightmapSettings& o) const
        {
            return texelsPerMeter == o.texelsPerMeter && minResolution == o.minResolution && maxResolution == o.maxResolution &&
                   maxPageSize == o.maxPageSize && samples == o.samples && maxBounces == o.maxBounces && denoise == o.denoise &&
                   fireflyClamp == o.fireflyClamp && indirectIntensity == o.indirectIntensity && denoiseIterations == o.denoiseIterations &&
                   denoiseLuminanceSigma == o.denoiseLuminanceSigma && denoiseNormalPower == o.denoiseNormalPower && denoisePlaneSigma == o.denoisePlaneSigma;
        }
    };

    constexpr const char* kLightmapExtension = ".lightmap";

    // Irradiance as GL_RGB9_E5 texels (Core::PackRGB9E5), bottom row first like a GL texture.
    struct LightmapPageData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint32_t> texels;
    };

    struct LightmapData
    {
        std::vector<LightmapPageData> pages;
    };

    bool WriteLightmapFile(const Filesystem::Path& path, const LightmapData& data);

    std::shared_ptr<LightmapData> DecodeLightmapFile(const Filesystem::Path& path);

    constexpr float kLightmapHole = 0.25f;

    // rgba is RGBA32F, bottom row first. Alpha is coverage: >0.5 baked, kLightmapHole given up, 0 no surface. Gutter rings extend covered texels so bilinear at chart edges doesn't blend in black.
    LightmapPageData EncodeLightmapPage(uint32_t width, uint32_t height, const float* rgba, int gutterPasses = 4);
}

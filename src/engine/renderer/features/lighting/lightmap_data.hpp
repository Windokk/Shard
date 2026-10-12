#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Rendering {

    /// What a bake of the lightmaps is asked for : how sharp, how clean, how deep.
    struct LightmapSettings
    {
        /// How many texels one meter of surface gets, on every mesh (it sets the resolution of each instance, see
        /// PlanLightmapLayout). 32 is a sharp shadow edge on a wall seen from a few meters ; 8 is the soft, low-frequency
        /// light of a large outdoor surface.
        float texelsPerMeter = 32.0f;

        /// The smallest and largest lightmap one instance can have, in texels per side (an instance that would need more is
        /// given its maximum, at a lower density)
        uint32_t minResolution = 16;
        uint32_t maxResolution = 1024;

        /// Side of the atlas pages, at most : the lightmaps are packed into one page, or several of this size when they do
        /// not fit. A page costs 4 bytes per texel on disk and in video memory.
        uint32_t maxPageSize = 2048;

        /// Paths traced per texel, and how many times each can bounce. The noise falls with the square root of the samples ;
        /// the denoiser takes most of what is left.
        uint32_t samples = 256;
        uint32_t maxBounces = 5;

        /// A multiplier on the light the surfaces bounce onto each other and the sky brings in (everything that is not a Static
        /// light's own direct light), as Unreal's Lightmass "indirect lighting intensity". 1 is the physical amount ; above
        /// it fills the shadows up for a brighter, flatter look without adding a light.
        float indirectIntensity = 1.0f;

        /// Filters the noise of the bake (an edge aware filter guided by the surface's position and normal)
        bool denoise = true;

        /// A path that brings back more light than this (luminance) is scaled down to it : the rare very bright one is a
        /// white speck in a lightmap, which no number of samples removes. 0 : no limit.
        float fireflyClamp = 32.0f;

        bool operator==(const LightmapSettings& o) const
        {
            return texelsPerMeter == o.texelsPerMeter && minResolution == o.minResolution && maxResolution == o.maxResolution &&
                   maxPageSize == o.maxPageSize && samples == o.samples && maxBounces == o.maxBounces && denoise == o.denoise &&
                   fireflyClamp == o.fireflyClamp && indirectIntensity == o.indirectIntensity;
        }
    };

    /// Extension of the file the baked pages of a world are stored in (registered in the asset database like any other
    /// project resource - see RenderWorldData's lightmap)
    constexpr const char* kLightmapExtension = ".lightmap";

    /// One page of the baked atlas : the irradiance every lightmapped surface of it receives, as the 32 bit shared exponent
    /// texels the GPU reads directly (GL_RGB9_E5, see Core::PackRGB9E5), row after row, the bottom row first like a GL texture
    struct LightmapPageData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint32_t> texels;
    };

    /// The CPU side of a baked world : its pages. Which part of which page each model uses is stored in the model itself.
    struct LightmapData
    {
        std::vector<LightmapPageData> pages;
    };

    /// Writes `data` to `path` (creating its parent directory). Returns false on any I/O failure or when a page's texels do
    /// not match its size.
    bool WriteLightmapFile(const Filesystem::Path& path, const LightmapData& data);

    /// Pure CPU read + validation of a lightmap file - no GL calls, so it is safe on a worker thread. Returns null if the
    /// file is missing, truncated, from another format version, or internally inconsistent.
    std::shared_ptr<LightmapData> DecodeLightmapFile(const Filesystem::Path& path);

    /// @brief Turns a baked page into what is stored and uploaded. `rgba` is width * height RGBA32F texels, bottom row first,
    /// its alpha the coverage : above 0.5 a texel was baked, below it belongs to no surface. The texels next to a covered
    /// one take its light (`gutterPasses` rings of them), so that the bilinear filter at the edge of a chart blends the
    /// light of the surface instead of the black around it ; the others stay black.
    LightmapPageData EncodeLightmapPage(uint32_t width, uint32_t height, const float* rgba, int gutterPasses = 4);
}

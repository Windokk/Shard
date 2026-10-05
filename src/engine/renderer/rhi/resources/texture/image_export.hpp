#pragma once

#include <cstdint>
#include <memory>

#include "engine/filesystem/filesystem.hpp"

namespace Shard::Engine::Rendering{

    class Texture2D;

    namespace ImageExport{

        // Writes an 8-bit-per-channel image to disk as PNG. `channels` must be 1-4 (grey / grey-alpha /
        // rgb / rgba), `data` must contain width * height * channels bytes, tightly packed.
        bool WritePNG(const Filesystem::Path& path, uint32_t width, uint32_t height, uint32_t channels, const uint8_t* data);

        // Writes a 32-bit float image to disk as a Radiance .hdr file. `channels` must be 1-4, `data`
        // must contain width * height * channels floats, tightly packed.
        bool WriteHDR(const Filesystem::Path& path, uint32_t width, uint32_t height, uint32_t channels, const float* data);

        // Convenience for the common offline-render case : reads back a texture using a float internal
        // format (RGBA32F, RGB16F, ...) and writes it straight to a .hdr file. Fails (returns false) if
        // the texture doesn't use a float format - use ReadPixels + WritePNG for 8-bit textures instead.
        bool WriteTextureAsHDR(const std::shared_ptr<Texture2D>& texture, const Filesystem::Path& path, uint32_t level = 0);

        // Convenience for exporting an 8-bit-per-channel texture (RGBA8, RGB8, ...) straight to PNG.
        // Fails (returns false) if the texture uses a float or integer internal format.
        bool WriteTextureAsPNG(const std::shared_ptr<Texture2D>& texture, const Filesystem::Path& path, uint32_t level = 0);
    }
}

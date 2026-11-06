#pragma once

#include <cstdint>
#include <memory>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Rendering{

    class Texture2D;

    namespace ImageExport{

        bool WritePNG(const Filesystem::Path& path, uint32_t width, uint32_t height, uint32_t channels, const uint8_t* data);

        bool WriteHDR(const Filesystem::Path& path, uint32_t width, uint32_t height, uint32_t channels, const float* data);

        bool WriteTextureAsHDR(const std::shared_ptr<Texture2D>& texture, const Filesystem::Path& path, uint32_t world = 0);

        bool WriteTextureAsPNG(const std::shared_ptr<Texture2D>& texture, const Filesystem::Path& path, uint32_t world = 0);
    }
}

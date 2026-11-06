#pragma once

#include "engine/assets/vfs/filesystem.hpp"

#include <cstdint>
#include <vector>

namespace Shard::Editor::GUI {

    // Bump when the look changes, it is part of the disk cache key of sound thumbnails.
    constexpr uint32_t kSoundThumbnailVersion = 1;

    bool DrawSoundThumbnail(const Engine::Filesystem::Path& file, std::vector<uint8_t>& rgba);
}

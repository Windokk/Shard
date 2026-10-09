#pragma once

#include "engine/assets/vfs/filesystem.hpp"

#include <cstdint>
#include <vector>

namespace Shard::Editor::GUI {

    /// Bump when the look of DrawSoundThumbnail changes (colors, layout, normalization) : it is part of the
    /// disk cache key of sound thumbnails, so the old ones are redrawn instead of shown stale.
    constexpr uint32_t kSoundThumbnailVersion = 1;

    /// @brief Draws a sound's waveform, Unity / Unreal style : peaks with the louder-on-average part (RMS) inside
    /// them, one lane per channel for stereo. It is the ThumbnailGenerator of the sound thumbnails, so it runs on a
    /// worker thread.
    bool DrawSoundThumbnail(const Engine::Filesystem::Path& file, std::vector<uint8_t>& rgba);
}

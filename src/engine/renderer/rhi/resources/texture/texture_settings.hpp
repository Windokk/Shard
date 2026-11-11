#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"

namespace Shard::Engine::Rendering {

    enum class TextureSizeMode
    {
        Original,
        NearestPowerOfTwo,
        NextPowerOfTwo,
        PreviousPowerOfTwo,
        Custom
    };

    // Stored next to the image as <image>.texsettings. An image without that file loads with the defaults below.
    struct TextureImportSettings
    {
        TextureSizeMode sizeMode = TextureSizeMode::Original;
        uint32_t customWidth = 0;
        uint32_t customHeight = 0;
        // Biggest side kept, in texels. 0 : no limit.
        uint32_t maxSize = 0;

        bool compress = true;
        bool generateMips = true;

        TextureFilter minFilter = TextureFilter::LinearMipmapLinear;
        TextureFilter magFilter = TextureFilter::Linear;
        TextureWrap wrapS = TextureWrap::Repeat;
        TextureWrap wrapT = TextureWrap::Repeat;

        bool operator==(const TextureImportSettings& other) const;
        bool operator!=(const TextureImportSettings& other) const { return !(*this == other); }

        // What changes the decoded pixels (size, compression), as opposed to how they are sampled.
        uint64_t PixelHash() const;
    };

    constexpr const char* kTextureSettingsExtension = ".texsettings";

    bool IsTextureSettingsFile(const std::string& path);

    // "<dir>/<name>.texsettings" -> "<dir>/<name>", empty when the path is not a settings file.
    std::string ImagePathOfSettingsFile(const std::string& path);

    Filesystem::Path TextureSettingsPath(const Filesystem::Path& image);

    TextureImportSettings LoadTextureSettings(const Filesystem::Path& image);

    // The defaults are stored as no file at all.
    bool SaveTextureSettings(const Filesystem::Path& image, const TextureImportSettings& settings);

    // Minification needing mips falls back to its non mipmapped twin when the texture has none.
    void ApplyTextureSettings(const TextureImportSettings& settings, TextureSpecifications& specs);

    bool IsPowerOfTwo(uint32_t value);
    uint32_t NearestPowerOfTwo(uint32_t value);
    uint32_t NextPowerOfTwo(uint32_t value);
    uint32_t PreviousPowerOfTwo(uint32_t value);

    void ResolveTextureSize(const TextureImportSettings& settings, uint32_t width, uint32_t height, uint32_t& outWidth, uint32_t& outHeight);

    // Triangle filter, 8 bit channels. The pixels are replaced.
    bool ResizeImage(std::vector<unsigned char>& pixels, uint32_t width, uint32_t height, uint32_t channels, uint32_t newWidth, uint32_t newHeight);

    // DecodeTextureFile, then the size the settings give.
    TextureDecodeResult DecodeTextureFile(const Filesystem::Path& filepath, const TextureImportSettings& settings);

    TextureDecodeResult DecodeTextureFileForUpload(const Filesystem::Path& filepath, const TextureImportSettings& settings, unsigned threads = 1, bool srgb = false);

    struct TextureFileInfo
    {
        bool valid = false;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t channels = 0;
    };

    TextureFileInfo ReadTextureFileInfo(const Filesystem::Path& image);

    // Video memory of the texture the settings give, estimated : block compressed when the loader would, with its mip chain.
    uint64_t EstimateTextureBytes(const TextureImportSettings& settings, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t channels);

    const char* TextureFilterName(TextureFilter filter);
    const char* TextureWrapName(TextureWrap wrap);
    const char* TextureSizeModeName(TextureSizeMode mode);
}

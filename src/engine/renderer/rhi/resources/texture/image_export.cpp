#include "image_export.hpp"

#include "engine/renderer/rhi/resources/texture/texture.hpp"

#include "engine/core/diagnostics/logger.hpp"

#include <stb/stb_image_write.h>

#include <algorithm>
#include <vector>

namespace Shard::Engine::Rendering::ImageExport{

    static uint32_t ChannelCount(TextureInternalFormat format)
    {
        switch (format)
        {
            case TextureInternalFormat::RED:
            case TextureInternalFormat::R16F:
                return 1;

            case TextureInternalFormat::RG:
            case TextureInternalFormat::RG16F:
                return 2;

            case TextureInternalFormat::RGB:
            case TextureInternalFormat::RGB8:
            case TextureInternalFormat::RGB16F:
            case TextureInternalFormat::RGB32F:
            case TextureInternalFormat::RGB32I:
                return 3;

            case TextureInternalFormat::RGBA8:
            case TextureInternalFormat::RGBA:
            case TextureInternalFormat::RGBA16F:
            case TextureInternalFormat::RGBA32F:
            case TextureInternalFormat::RGBA32I:
                return 4;

            default:
                return 4;
        }
    }

    static bool IsFloatFormat(TextureInternalFormat format)
    {
        switch (format)
        {
            case TextureInternalFormat::R16F:
            case TextureInternalFormat::RG16F:
            case TextureInternalFormat::RGB16F:
            case TextureInternalFormat::RGB32F:
            case TextureInternalFormat::RGBA16F:
            case TextureInternalFormat::RGBA32F:
                return true;

            default:
                return false;
        }
    }

    static bool IsUnsignedByteFormat(TextureInternalFormat format)
    {
        switch (format)
        {
            case TextureInternalFormat::RED:
            case TextureInternalFormat::RG:
            case TextureInternalFormat::RGB:
            case TextureInternalFormat::RGB8:
            case TextureInternalFormat::RGBA:
            case TextureInternalFormat::RGBA8:
                return true;

            default:
                return false;
        }
    }

    bool WritePNG(const Filesystem::Path& path, uint32_t width, uint32_t height, uint32_t channels, const uint8_t* data)
    {
        if (channels < 1 || channels > 4)
        {
            DEBUG_ERROR("WritePNG : unsupported channel count (" + std::to_string(channels) + ") for " + path.full);
            return false;
        }

        // Textures are loaded with stbi_set_flip_vertically_on_load(true) (see Texture2D::Create), so
        // row 0 of a GL texture is its bottom row - flip on write too, or an exported image would come
        // out upside down relative to every other image the engine loads.
        stbi_flip_vertically_on_write(1);

        int result = stbi_write_png(path.GetNativePath().c_str(), (int)width, (int)height, (int)channels, data, (int)(width * channels));

        if (!result)
            DEBUG_ERROR("Failed to write PNG : " + path.full);

        return result != 0;
    }

    bool WriteHDR(const Filesystem::Path& path, uint32_t width, uint32_t height, uint32_t channels, const float* data)
    {
        if (channels < 1 || channels > 4)
        {
            DEBUG_ERROR("WriteHDR : unsupported channel count (" + std::to_string(channels) + ") for " + path.full);
            return false;
        }

        stbi_flip_vertically_on_write(1);

        int result = stbi_write_hdr(path.GetNativePath().c_str(), (int)width, (int)height, (int)channels, data);

        if (!result)
            DEBUG_ERROR("Failed to write HDR : " + path.full);

        return result != 0;
    }

    bool WriteTextureAsHDR(const std::shared_ptr<Texture2D>& texture, const Filesystem::Path& path, uint32_t level)
    {
        if (!texture)
            return false;

        const TextureSpecifications& spec = texture->GetSpecifications();

        if (!IsFloatFormat(spec.internalFormat))
        {
            DEBUG_ERROR("WriteTextureAsHDR : texture for " + path.full + " does not use a float internal format.");
            return false;
        }

        uint32_t width = std::max(1u, spec.width >> level);
        uint32_t height = std::max(1u, spec.height >> level);
        uint32_t channels = ChannelCount(spec.internalFormat);

        size_t byteSize = texture->GetPixelDataSize(level);
        std::vector<float> pixels(byteSize / sizeof(float));

        texture->ReadPixels(pixels.data(), byteSize, level);

        return WriteHDR(path, width, height, channels, pixels.data());
    }

    bool WriteTextureAsPNG(const std::shared_ptr<Texture2D>& texture, const Filesystem::Path& path, uint32_t level)
    {
        if (!texture)
            return false;

        const TextureSpecifications& spec = texture->GetSpecifications();

        if (!IsUnsignedByteFormat(spec.internalFormat))
        {
            DEBUG_ERROR("WriteTextureAsPNG : texture for " + path.full + " does not use an 8-bit-per-channel internal format.");
            return false;
        }

        uint32_t width = std::max(1u, spec.width >> level);
        uint32_t height = std::max(1u, spec.height >> level);
        uint32_t channels = ChannelCount(spec.internalFormat);

        size_t byteSize = texture->GetPixelDataSize(level);
        std::vector<uint8_t> pixels(byteSize);

        texture->ReadPixels(pixels.data(), byteSize, level);

        return WritePNG(path, width, height, channels, pixels.data());
    }
}

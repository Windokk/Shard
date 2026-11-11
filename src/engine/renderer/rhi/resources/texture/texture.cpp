#include "texture.hpp"
#include "engine/renderer/rhi/render_thread.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/backends/opengl/texture/gl_texture.hpp"
#include "engine/renderer/rhi/backends/null/null_rhi.hpp"
#include "engine/renderer/rhi/backends/vulkan/vk_texture.hpp"

#include <stb/stb_image.h>

#include "engine/renderer/rhi/renderer_api.hpp"

#include "engine/core/diagnostics/logger.hpp"
#include "engine/core/config/cvar.hpp"

#include <algorithm>
#include <chrono>

namespace Shard::Engine::Rendering{

    namespace {
        Core::CVar<int> cvTextureMaxSize("texture.max_size", 0,
            "Biggest side, in texels, a texture keeps in video memory (the bigger levels are left out when it is uploaded) : 2048 divides the memory of 4096 textures by 4. 0 : no limit. Applies to textures loaded afterwards",
            Core::CVarFlags::Archive, 0, 16384);

        Core::CVar<bool> cvTextureCompress("texture.compress", true,
            "Block compress (BC7 / BC4 / BC5) the textures when they are loaded : a quarter to an eighth of the video memory. Applies to textures loaded afterwards",
            Core::CVarFlags::Archive);
    }

    static size_t BytesPerPixel(TextureInternalFormat format)
    {
        switch (format)
        {
            case TextureInternalFormat::RED:              return 1;
            case TextureInternalFormat::R16F:              return 2;
            case TextureInternalFormat::R32F:              return 4;
            case TextureInternalFormat::RG:                return 2;
            case TextureInternalFormat::RG16F:             return 4;
            case TextureInternalFormat::RGB:
            case TextureInternalFormat::RGB8:              return 3;
            case TextureInternalFormat::RGB16F:            return 6;
            case TextureInternalFormat::RGB32I:
            case TextureInternalFormat::RGB32F:            return 12;
            case TextureInternalFormat::RGBA8:
            case TextureInternalFormat::RGBA:              return 4;
            case TextureInternalFormat::RGB9E5:            return 4;
            case TextureInternalFormat::RGBA16F:           return 8;
            case TextureInternalFormat::RGBA32I:
            case TextureInternalFormat::RGBA32F:           return 16;
            case TextureInternalFormat::Depth16:           return 2;
            case TextureInternalFormat::Depth24:
            case TextureInternalFormat::Depth32:
            case TextureInternalFormat::Depth32F:
            case TextureInternalFormat::Depth24Stencil8:   return 4;
            default:                                       return 4;
        }
    }

    size_t Texture2D::GetPixelDataSize(uint32_t world) const
    {
        uint32_t width = std::max(1u, m_Specifications.width >> world);
        uint32_t height = std::max(1u, m_Specifications.height >> world);

        return (size_t)width * (size_t)height * BytesPerPixel(m_Specifications.internalFormat);
    }

    std::shared_ptr<Texture2D> Texture2D::Create(TextureSpecifications &spec, const void *data)
    {
        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return OnGraphicsThread([&]() -> std::shared_ptr<Texture2D> { return std::make_shared<GLTexture2D>(spec, data); });

            case RendererAPI::API::Vulkan:
                return OnGraphicsThread([&]() -> std::shared_ptr<Texture2D> { return std::make_shared<VKTexture2D>(spec, data); });

            case RendererAPI::API::Null:
                return std::make_shared<Null::NullTexture2D>(spec, data);

            /*case RendererAPI::API::Vulkan:
                return std::make_shared<VKTexture>(spec, data);

            case RendererAPI::API::DX11:
                return std::make_shared<DX11Texture>(spec, data);

            case RendererAPI::API::DX12:
                return std::make_shared<DX12Texture>(spec, data);*/

            default:
                return nullptr;
        }
    }

    TextureDecodeResult DecodeTextureFile(const Filesystem::Path &filepath)
    {
        TextureDecodeResult result;

        if (!filepath.Exists()) {
            DEBUG_ERROR("Couldn't find texture : " + filepath.full);
            return result;
        }

        std::string file = filepath.ReadFile();

        // The non-thread variant sets a plain global, UB when called concurrently. This one is safe from decode workers.
        stbi_set_flip_vertically_on_load_thread(true);

        int width, height, nrChannels;
        unsigned char* data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(file.data()),
                                        static_cast<int>(file.size()),
                                        &width, &height, &nrChannels, 0);

        if (!data) {
            DEBUG_ERROR("Couldn't load texture : " + filepath.full);
            return result;
        }

        TextureInternalFormat format;
        if (nrChannels == 1){
            format = TextureInternalFormat::RED;
        }
        else if (nrChannels == 2){
            format = TextureInternalFormat::RG;
        }
        else if (nrChannels == 3){
            format = TextureInternalFormat::RGB;
        }
        else if (nrChannels == 4){
            format = TextureInternalFormat::RGBA;
        }
        else{
            format = TextureInternalFormat::RGB;
        }

        size_t dataSize = (size_t)width * (size_t)height * (size_t)nrChannels;
        result.pixels.assign(data, data + dataSize);
        stbi_image_free(data);

        result.success = true;
        result.width = width;
        result.height = height;
        result.format = format;

        return result;
    }

    TextureDecodeResult DecodeTextureFileForUpload(const Filesystem::Path& filepath, unsigned threads, bool srgb)
    {
        // Null backend draws nothing, so skip decoding. The file is still looked up: a missing asset is an error everywhere.
        if (RendererAPI::Current() && RendererAPI::Current()->GetAPI() == RendererAPI::API::Null)
        {
            TextureDecodeResult stub;
            if (!filepath.Exists())
            {
                DEBUG_ERROR("Couldn't find texture : " + filepath.full);
                return stub;
            }
            stub.success = true;
            stub.width = stub.height = 1;
            stub.format = TextureInternalFormat::RGBA8;
            stub.pixels = {0, 0, 0, 255};
            return stub;
        }

        auto fromCompressed = [](std::shared_ptr<CompressedImage> cached) {
            TextureDecodeResult result;
            result.success = true;
            result.width = int(cached->width);
            result.height = int(cached->height);
            result.format = cached->format == BlockFormat::BC4 ? TextureInternalFormat::RED
                          : cached->format == BlockFormat::BC5 ? TextureInternalFormat::RG : TextureInternalFormat::RGBA;
            result.compressed = std::move(cached);
            return result;
        };

        // Cooked next to the file : no decode, no compression. The other variant is better than nothing (a texture used both as colour and as data).
        for (const bool wantSrgb : { srgb, !srgb })
        {
            auto cooked = std::make_shared<CompressedImage>();
            if (TextureCache::Load(TextureCache::CookedPath(filepath.GetNativePath(), wantSrgb), *cooked))
                return fromCompressed(std::move(cooked));
        }

        if (!cvTextureCompress.Get())
            return DecodeTextureFile(filepath);

        const std::string key = TextureCache::MakeKey(filepath.full, srgb);
        if (!key.empty())
        {
            auto cached = std::make_shared<CompressedImage>();
            if (TextureCache::Load(key, *cached))
            {
                TextureDecodeResult result;
                result.success = true;
                result.width = int(cached->width);
                result.height = int(cached->height);
                result.format = cached->format == BlockFormat::BC4 ? TextureInternalFormat::RED
                              : cached->format == BlockFormat::BC5 ? TextureInternalFormat::RG : TextureInternalFormat::RGBA;
                result.compressed = std::move(cached);
                return result;
            }
        }

        TextureDecodeResult result = DecodeTextureFile(filepath);
        if (!result.success || !ShouldCompress(uint32_t(result.width), uint32_t(result.height)))
            return result;

        const uint32_t channels = BytesPerPixel(result.format);
        if (channels < 1 || channels > 4 || result.pixels.size() != size_t(result.width) * result.height * channels)
            return result;

        const bool normalMap = LooksLikeNormalMap(result.pixels.data(), uint32_t(result.width), uint32_t(result.height), channels);
        auto compressed = std::make_shared<CompressedImage>();
        if (!CompressImage(result.pixels.data(), uint32_t(result.width), uint32_t(result.height), channels, *compressed, normalMap, threads, srgb))
            return result;

        TextureCache::Store(key, *compressed);

        result.compressed = std::move(compressed);
        std::vector<unsigned char>().swap(result.pixels);
        return result;
    }

    bool CookTexture(const Filesystem::Path& filepath, bool srgb, unsigned threads, std::vector<uint8_t>& out)
    {
        TextureDecodeResult result = DecodeTextureFile(filepath);
        if (!result.success || !ShouldCompress(uint32_t(result.width), uint32_t(result.height)))
            return false;

        const uint32_t channels = BytesPerPixel(result.format);
        if (channels < 1 || channels > 4 || result.pixels.size() != size_t(result.width) * result.height * channels)
            return false;

        const bool normalMap = LooksLikeNormalMap(result.pixels.data(), uint32_t(result.width), uint32_t(result.height), channels);
        CompressedImage compressed;
        if (!CompressImage(result.pixels.data(), uint32_t(result.width), uint32_t(result.height), channels, compressed, normalMap, threads, srgb))
            return false;
        out = TextureCache::Serialize(compressed);
        return true;
    }

    uint32_t MipsToSkip(uint32_t width, uint32_t height)
    {
        const int limit = cvTextureMaxSize.Get();
        if (limit <= 0)
            return 0;

        uint32_t skip = 0;
        uint32_t side = std::max(width, height);
        while (side > uint32_t(limit) && side > 4)
        {
            side >>= 1;
            skip++;
        }
        return skip;
    }

    std::shared_ptr<Texture2D> Texture2D::Create(const TextureSpecifications& spec, const CompressedImage& image, uint32_t firstMip)
    {
        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return OnGraphicsThread([&]() -> std::shared_ptr<Texture2D> { return std::make_shared<GLTexture2D>(spec, image, firstMip); });
            case RendererAPI::API::Vulkan:
                return OnGraphicsThread([&]() -> std::shared_ptr<Texture2D> { return std::make_shared<VKTexture2D>(spec, image, firstMip); });
            case RendererAPI::API::Null:
                return std::make_shared<Null::NullTexture2D>(spec, image, firstMip);
            default:
                return nullptr;
        }
    }

    std::shared_ptr<Texture2D> Texture2D::Create(TextureSpecifications& spec, const TextureDecodeResult& decoded)
    {
        if (!decoded.success)
            return nullptr;

        if (decoded.compressed)
            return Create(static_cast<const TextureSpecifications&>(spec), *decoded.compressed, MipsToSkip(decoded.compressed->width, decoded.compressed->height));

        spec.internalFormat = decoded.format;
        spec.width = decoded.width;
        spec.height = decoded.height;
        return Create(spec, decoded.pixels.data());
    }

    std::shared_ptr<Texture2D> Texture2D::Create(TextureSpecifications &spec, const Filesystem::Path &filepath)
    {
        TextureDecodeResult decoded = DecodeTextureFile(filepath);

        if (!decoded.success) {
            return nullptr;
        }

        spec.internalFormat = decoded.format;
        spec.width = decoded.width;
        spec.height = decoded.height;

        return Create(spec, decoded.pixels.data());
    }
}
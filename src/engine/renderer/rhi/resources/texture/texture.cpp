#include "texture.hpp"

#include "engine/core/engine.hpp"

#include "engine/renderer/rhi/backends/opengl/texture/gl_texture.hpp"

#include <stb/stb_image.h>

#include "engine/renderer/frontend/renderer.hpp"

#include "engine/debugging/logger.hpp"

#include <algorithm>

namespace Shard::Engine::Rendering{

    // Bytes per pixel for each internal format, mirroring the format/type resolved by
    // GLTextureSpec::FromTextureSpecifications (kept here rather than in the OpenGL backend since it's
    // purely a function of the API-agnostic TextureInternalFormat, needed to size ReadPixels buffers).
    static size_t BytesPerPixel(TextureInternalFormat format)
    {
        switch (format)
        {
            case TextureInternalFormat::RED:              return 1;
            case TextureInternalFormat::R16F:              return 2;
            case TextureInternalFormat::RG:                return 2;
            case TextureInternalFormat::RG16F:             return 4;
            case TextureInternalFormat::RGB:
            case TextureInternalFormat::RGB8:              return 3;
            case TextureInternalFormat::RGB16F:            return 6;
            case TextureInternalFormat::RGB32I:
            case TextureInternalFormat::RGB32F:            return 12;
            case TextureInternalFormat::RGBA8:
            case TextureInternalFormat::RGBA:              return 4;
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

    size_t Texture2D::GetPixelDataSize(uint32_t level) const
    {
        uint32_t width = std::max(1u, m_Specifications.width >> level);
        uint32_t height = std::max(1u, m_Specifications.height >> level);

        return (size_t)width * (size_t)height * BytesPerPixel(m_Specifications.internalFormat);
    }

    std::shared_ptr<Texture2D> Texture2D::Create(TextureSpecifications &spec, const void *data)
    {
        switch(Core::GetEngine().GetRenderer()->GetRendererAPI()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return std::make_shared<GLTexture2D>(spec, data);

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

        // stbi_set_flip_vertically_on_load() sets a plain (non-thread-local) global and is UB if
        // called concurrently from multiple threads; the _thread variant is the one safe to call from
        // a background decode worker.
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
            format = TextureInternalFormat::RGB; // Default to RGB
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
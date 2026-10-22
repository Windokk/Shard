#include "cubemap.hpp"
#include "engine/renderer/rhi/render_thread.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/backends/opengl/texture/cubemap/gl_cubemap.hpp"
#include "engine/renderer/rhi/backends/null/null_rhi.hpp"
#include "engine/renderer/rhi/backends/vulkan/vk_cubemap.hpp"

#include <stb/stb_image.h>

#include "engine/renderer/rhi/renderer_api.hpp"

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Rendering{
    std::shared_ptr<Cubemap> Cubemap::Create(const TextureSpecifications& specs, std::array<unsigned char*, 6> faces)
    {
        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return OnGraphicsThread([&]() -> std::shared_ptr<Cubemap> { return std::make_shared<GLCubemap>(specs, faces); });

            case RendererAPI::API::Vulkan:
                return OnGraphicsThread([&]() -> std::shared_ptr<Cubemap> { return std::make_shared<VKCubemap>(specs, faces); });

            case RendererAPI::API::Null:
                return std::make_shared<Null::NullCubemap>(specs);

            /*case API::Vulkan:
                return std::make_shared<VKCubemap>(faces);

            case API::DX11:
                return std::make_shared<DX11Cubemap>(faces);

            case API::DX12:
                return std::make_shared<DX12Cubemap>(faces);*/

            default:
                return nullptr;
        }
    }

    std::shared_ptr<Cubemap> Cubemap::Create(TextureSpecifications& specs, const std::array<Filesystem::Path, 6> imageFiles)
    {
        std::array<unsigned char*, 6> faces;

        int width = 0, height = 0, channels = 0;
        int firstWidth = 0, firstHeight = 0, firstChannels = 0;

        for (size_t i = 0; i < 6; i++)
        {
            Filesystem::Path filePath = imageFiles[i];

            if(!filePath.Exists() || filePath.IsDirectory()){
                DEBUG_ERROR("Attempted to create a cubemap with a missing file : ",filePath.full);
                for (size_t j = 0; j < i; j++)
                    stbi_image_free(faces[j]);
                return nullptr;
            }

            std::string file = filePath.ReadFile();

            faces[i] = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(file.data()),
                                            static_cast<int>(file.size()),
                                            &width, &height, &channels, 0);

            if (faces[i] && i > 0 && (width != firstWidth || height != firstHeight || channels != firstChannels))
            {
                DEBUG_ERROR("The faces of a cubemap must share a size and a channel count : ", filePath.full);
                stbi_image_free(faces[i]);
                faces[i] = nullptr;
            }

            if (!faces[i])
            {
                for (size_t j = 0; j < i; j++)
                    stbi_image_free(faces[j]);

                return nullptr;
            }

            if (i == 0)
            {
                firstWidth = width;
                firstHeight = height;
                firstChannels = channels;
            }
        }

        // What the pictures hold is what the texture is made of : the caller's specifications only say how it is filtered and read
        specs.width = static_cast<uint32_t>(firstWidth);
        specs.height = static_cast<uint32_t>(firstHeight);
        switch (firstChannels)
        {
            case 1: specs.format = TextureFormat::RED;  specs.internalFormat = TextureInternalFormat::RED;   break;
            case 2: specs.format = TextureFormat::RG;   specs.internalFormat = TextureInternalFormat::RG;    break;
            case 3: specs.format = TextureFormat::RGB;  specs.internalFormat = TextureInternalFormat::RGB8;  break;
            default: specs.format = TextureFormat::RGBA; specs.internalFormat = TextureInternalFormat::RGBA8; break;
        }

        auto cubemap = Create(specs, faces);

        for (int i = 0; i < 6; i++)
            stbi_image_free(faces[i]);

        return cubemap;
    }

    std::shared_ptr<CubemapArray> CubemapArray::Create(const TextureSpecifications &specs, std::vector<std::array<unsigned char *, 6>> data)
    {
        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return OnGraphicsThread([&]() -> std::shared_ptr<CubemapArray> { return std::make_shared<GLCubemapArray>(specs, data); });

            case RendererAPI::API::Vulkan:
                return OnGraphicsThread([&]() -> std::shared_ptr<CubemapArray> { return std::make_shared<VKCubemapArray>(specs, data); });

            case RendererAPI::API::Null:
                return std::make_shared<Null::NullCubemapArray>(specs, static_cast<uint32_t>(data.size()));

            /*case API::Vulkan:
                return std::make_shared<VKCubemap>(faces);

            case API::DX11:
                return std::make_shared<DX11Cubemap>(faces);

            case API::DX12:
                return std::make_shared<DX12Cubemap>(faces);*/

            default:
                return nullptr;
        }
    }

    std::shared_ptr<CubemapArray> CubemapArray::Create(TextureSpecifications& specs, const std::vector<std::array<Filesystem::Path, 6>> imageFiles)
    {
        // Every face of every cubemap of the array is one layer of the same texture : they all share a size and a channel count
        std::vector<std::array<unsigned char *, 6>> allData;
        int firstWidth = 0, firstHeight = 0, firstChannels = 0;

        auto freeAll = [&allData](){
            for (auto& faces : allData)
                for (unsigned char* face : faces)
                    stbi_image_free(face);
        };

        for(size_t k = 0; k < imageFiles.size(); k++){
            allData.emplace_back();
            allData.back().fill(nullptr);
            std::array<unsigned char*, 6>& faces = allData.back();

            for (size_t i = 0; i < 6; i++)
            {
                Filesystem::Path filePath = imageFiles[k][i];

                if(!filePath.Exists() || filePath.IsDirectory()){
                    DEBUG_ERROR("Attempted to create a cubemap with a missing file : ",filePath.full);
                    freeAll();
                    return nullptr;
                }

                std::string file = filePath.ReadFile();

                int width = 0, height = 0, channels = 0;
                faces[i] = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(file.data()),
                                                static_cast<int>(file.size()),
                                                &width, &height, &channels, 0);

                if (!faces[i])
                {
                    DEBUG_ERROR("Couldn't decode a face of a cubemap : ", filePath.full);
                    freeAll();
                    return nullptr;
                }

                if (k == 0 && i == 0)
                {
                    firstWidth = width;
                    firstHeight = height;
                    firstChannels = channels;
                }
                else if (width != firstWidth || height != firstHeight || channels != firstChannels)
                {
                    DEBUG_ERROR("The faces of a cubemap array must share a size and a channel count : ", filePath.full);
                    freeAll();
                    return nullptr;
                }
            }
        }

        // As Cubemap::Create does : what the pictures hold is what the texture is made of
        if (!allData.empty())
        {
            specs.width = static_cast<uint32_t>(firstWidth);
            specs.height = static_cast<uint32_t>(firstHeight);
            switch (firstChannels)
            {
                case 1: specs.format = TextureFormat::RED;  specs.internalFormat = TextureInternalFormat::RED;   break;
                case 2: specs.format = TextureFormat::RG;   specs.internalFormat = TextureInternalFormat::RG;    break;
                case 3: specs.format = TextureFormat::RGB;  specs.internalFormat = TextureInternalFormat::RGB8;  break;
                default: specs.format = TextureFormat::RGBA; specs.internalFormat = TextureInternalFormat::RGBA8; break;
            }
        }

        auto cubemapArray = CubemapArray::Create(specs, allData);

        freeAll();

        return cubemapArray;
    }
}
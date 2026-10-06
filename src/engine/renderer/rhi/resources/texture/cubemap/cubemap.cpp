#include "cubemap.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/backends/opengl/texture/cubemap/gl_cubemap.hpp"

#include <stb/stb_image.h>

#include "engine/renderer/rhi/renderer_api.hpp"

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Rendering{
    std::shared_ptr<Cubemap> Cubemap::Create(const TextureSpecifications& specs, std::array<unsigned char*, 6> faces)
    {
        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return std::make_shared<GLCubemap>(specs, faces);

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

        int width, height, channels;

        for (size_t i = 0; i < 6; i++)
        {
            Filesystem::Path filePath = imageFiles[i];

            if(!filePath.Exists() || filePath.IsDirectory()){
                DEBUG_ERROR("Attempted to create a cubemap with a missing file : ",filePath.full);
                return nullptr;
            }

            std::string file = filePath.ReadFile();
            
            faces[i] = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(file.data()),
                                            static_cast<int>(file.size()),
                                            &width, &height, &channels, 0);

            if (!faces[i])
            {
                for (size_t j = 0; j < i; j++)
                    stbi_image_free(faces[j]);

                return nullptr;
            }
        }

        auto cubemap = Create(specs, faces);

        //TODO : Set specifications from data's average ? or max ? or min ?
        // specs.width = 
        // specs.height = 
        // specs.format = 

        for (int i = 0; i < 6; i++)
            stbi_image_free(faces[i]);

        return cubemap;
    }

    std::shared_ptr<CubemapArray> CubemapArray::Create(const TextureSpecifications &specs, std::vector<std::array<unsigned char *, 6>> data)
    {
        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return std::make_shared<GLCubemapArray>(specs, data);

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
        std::vector<std::array<unsigned char *, 6>> allData;

        for(int k = 0; k < imageFiles.size(); k++){
            std::array<unsigned char*, 6> faces;

            int width, height, channels;

            for (size_t i = 0; i < 6; i++)
            {
                Filesystem::Path filePath = imageFiles[k][i];

                if(!filePath.Exists() || filePath.IsDirectory()){
                    DEBUG_ERROR("Attempted to create a cubemap with a missing file : ",filePath.full);
                    return nullptr;
                }

                std::string file = filePath.ReadFile();
                
                int currentWidth, currentHeight, currentChannels;

                faces[i] = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(file.data()),
                                                static_cast<int>(file.size()),
                                                &currentWidth, &currentHeight, &currentChannels, 0);

                if(width != 0 && height != 0 && channels != 0 && (currentWidth != width || currentWidth != height || currentChannels != channels)){
                    DEBUG_ERROR("Attempted to create a cubemap with file using different width/height/channel number : ", filePath.full);
                    return nullptr;
                }
                else if(width == 0 && height == 0 && channels == 0){
                    width = currentWidth;
                    height = currentHeight;
                    channels = currentChannels;
                }

                if (!faces[i])
                {
                    for (size_t j = 0; j < i; j++)
                        stbi_image_free(faces[j]);

                    return nullptr;
                }
            }

            allData.push_back(faces);

            //TODO : Set specifications from data's average ? or max ? or min ?
            // specs.width = 
            // specs.height = 
            // specs.format = 

        }
    
        auto cubemapArray = CubemapArray::Create(specs, allData);

        for(int k = 0; k < imageFiles.size(); k++){
            for (int i = 0; i < 6; i++){
                stbi_image_free(allData[k][i]);
            }
        }

        return cubemapArray;
    }
}
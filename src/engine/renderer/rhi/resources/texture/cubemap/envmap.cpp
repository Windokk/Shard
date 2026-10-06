#include "envmap.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/backends/opengl/texture/cubemap/gl_envmap.hpp"

#include "engine/renderer/rhi/renderer_api.hpp"

namespace Shard::Engine::Rendering{
    
    std::shared_ptr<EnvironmentMap> EnvironmentMap::Create(TextureSpecifications& specs, const std::vector<Filesystem::Path> imageFiles)
    {
        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:{
                GLEnvironmentMapGenerator glGenerator;
                return glGenerator.GenerateFromFiles(specs, imageFiles);
            }

            /*case API::Vulkan:
                return std::make_shared<VKCubemap>(spec, faces);

            case API::DX11:
                return std::make_shared<DX11Cubemap>(spec, faces);

            case API::DX12:
                return std::make_shared<DX12Cubemap>(spec, faces);*/

            default:
                return nullptr;
        }
    }
    std::shared_ptr<EnvironmentMap> EnvironmentMap::Create(TextureSpecifications &specs, const Filesystem::Path hdrFile)
    {
        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:{
                static GLEnvironmentMapGenerator glGenerator;
                return glGenerator.GenerateFromHDR(specs, hdrFile);
            }

            /*case API::Vulkan:
                return std::make_shared<VKCubemap>(spec, faces);

            case API::DX11:
                return std::make_shared<DX11Cubemap>(spec, faces);

            case API::DX12:
                return std::make_shared<DX12Cubemap>(spec, faces);*/

            default:
                return nullptr;
        }
    }
}
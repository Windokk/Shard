#include "material.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/frontend/renderer.hpp"

#include "engine/renderer/rhi/backends/opengl/material/gl_material.hpp"

#include "engine/renderer/material/shader.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"

namespace Shard::Engine::Rendering{

    std::shared_ptr<Material> Material::Create(std::shared_ptr<Shader> shader, std::shared_ptr<Pipeline> pipeline, bool receivesShadows, Opacity opacity)
    {
        if(!shader || !pipeline){
            DEBUG_ERROR("Attempted to create a material using an invalid shader and/or pipeline, aborting.");
            return nullptr;
        }

        if(shader->GetAssetID() != pipeline->GetSpecifications().shader->GetAssetID()){
            DEBUG_ERROR("Tried creating a material with different shaders. Specified shader and specified pipeline's shader are not the same !");
            return nullptr;
        }

        switch(Core::GetEngine().GetRenderer()->GetRendererAPI()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return std::make_shared<GLMaterial>(shader, pipeline, receivesShadows, opacity);

            /*case RendererAPI::API::Vulkan:
                return std::make_shared<VKFramebuffer>(spec);

            case RendererAPI::API::DX11:
                return std::make_shared<DX11Framebuffer>(spec);

            case RendererAPI::API::DX12:
                return std::make_shared<DX12Framebuffer>(spec);*/

            default:
                return nullptr;
        }
    }

}
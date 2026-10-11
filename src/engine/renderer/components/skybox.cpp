#include "skybox.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/resources/texture/cubemap/cubemap.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"

namespace Shard::Engine::Objects{

    Skybox::Skybox(std::shared_ptr<Rendering::EnvironmentMap> envMap, std::shared_ptr<Rendering::Material> material)
    {
        m_EnvMap = envMap;
        m_Material = material;
        CreateDrawCommands();
    }

    void Skybox::SetEnvironmentMap(std::shared_ptr<Rendering::EnvironmentMap> envMap)
    {
        m_EnvMap = envMap;
        CreateDrawCommands();
    }

    void Skybox::SetMaterial(std::shared_ptr<Rendering::Material> material)
    {
        m_Material = material;
        CreateDrawCommands();
    }

    void Skybox::Destroy()
    {
        WorldObject::Destroy();
    }

    void Skybox::CreateDrawCommands()
    {
        Rendering::PipelineSpecifications specs;
        specs.vertexLayout = {};
        specs.depthCompare = Rendering::DepthCompareOp::LessOrEqual;
        specs.depthWrite = false;
        specs.cullMode = Rendering::CullMode::Front;
        specs.shader = m_Material->GetShader();
        specs.topology = Rendering::PrimitiveTopology::Triangles;
        specs.debugName = "SkyboxPipeline";

        std::shared_ptr<Rendering::Pipeline> pipeline = Core::GetEngine().GetRenderContext()->GetOrAddPipeline(specs);
        m_Material->SetPipeline(pipeline);
        m_Material->SetTextureParameter("uSkybox", m_EnvMap->GetCubemap()->GetHandle());

        // The draw itself is the renderer's : the world's environment proxy carries this material, and the
        // renderer submits (or drops) the fullscreen triangle from the snapshot.
    }
}
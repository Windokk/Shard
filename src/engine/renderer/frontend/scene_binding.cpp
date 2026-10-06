#include "renderer.hpp"
#include "engine/renderer/rhi/material/material.hpp"

#include "engine/world/engine.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"
#include "engine/renderer/features/lighting/light_manager.hpp"
#include "engine/renderer/features/lighting/shadow_manager.hpp"
#include "engine/renderer/features/lighting/probe_manager.hpp"
#include "engine/renderer/features/lighting/ssao_manager.hpp"
#include "engine/renderer/features/lighting/light_culling_manager.hpp"
#include "engine/renderer/components/render_world_data.hpp"

// The Renderer's side of ISceneBinding : what the backend asks for while it issues a draw.

namespace Shard::Engine::Rendering{

    void Renderer::BindWorldState(RendererAPI& api, std::shared_ptr<Shader> shader, glm::mat4 modelMatrix, int objectID, bool applyPassGlobals)
    {
        // model/objID are genuinely per-object and must always be set.
        shader->SetMat4("model", modelMatrix);
        shader->SetInt("objID", objectID);

        shader->SetVec3("emissive", glm::vec3(0.0f));

        // Editor debug view (main scene view only - studio/immediate renders always look normal).
        {
            RenderView view;
            const bool studioView = GetCurrentView(view) && view.lighting == ViewLighting::Studio;
            const DebugViewState& dv = api.GetDebugView();
            shader->SetInt("viewMode", studioView ? 0 : static_cast<int>(dv.mode));
            shader->SetBool("showLighting", studioView || dv.showLighting);
            shader->SetBool("showShadows", studioView || dv.showShadows);
        }

        auto world = Core::GetEngine().GetWorldManager()->GetWorldAt(0);

        RenderView currentView;
        GetCurrentView(currentView);
        const bool studio = currentView.lighting == ViewLighting::Studio;

        if (studio)
        {
            // Fixed studio look : none of the world's lighting settings apply, and SSAO's texture is
            // the main viewport's (screen-space), which would be meaningless for another view.
            shader->SetFloat("ambientIntensity", currentView.studioAmbient);
            shader->SetBool("ssaoEnabled", false);
        }
        else if (world)
        {
            shader->SetFloat("ambientIntensity", world->ambientIntensity);

            shader->SetBool("ssaoEnabled", world->ssaoEnabled);
            shader->SetFloat("ssaoIntensity", world->ssaoIntensity);
        }

        if (!applyPassGlobals)
            return;

        const auto& uniforms = shader->GetActiveUniformsMap();
        auto it = uniforms.find("useEnvReflections");

        // A studio view brings its own environment so its look never depends on the world.
        std::shared_ptr<EnvironmentMap> envMap;
        if (studio)
            envMap = currentView.studioEnvironment;
        else if (world && world->Ext<RenderWorldData>().skybox)
            envMap = world->Ext<RenderWorldData>().skybox->GetEnvMap();

        if(it != uniforms.end() && envMap){
            //Bind skybox data
            const auto& samplers = shader->GetActiveSamplersMap();

            api.BindTextureUnit(samplers.find("ibl_irradianceMap")->second.binding, envMap->GetIrradiance()->GetHandle());

            api.BindTextureUnit(samplers.find("ibl_prefilteredEnvMap")->second.binding, envMap->GetPrefilter()->GetHandle());

            api.BindTextureUnit(samplers.find("ibl_brdfLUT")->second.binding, envMap->GetBRDFLUT()->GetHandle());
        }

        auto probeManager = GetProbeManager();
        bool ddgiReady = !studio && probeManager && probeManager->IsReady();
        shader->SetBool("ddgi_enabled", ddgiReady);

        if (ddgiReady)
        {
            const auto& samplers = shader->GetActiveSamplersMap();
            int volumeCount = 0;
            int lastReady = -1;

            for (int i = 0; i < probeManager->GetActiveVolumeCount(); i++)
            {
                if (!probeManager->IsVolumeReady(i))
                    continue;

                std::string idx = "[" + std::to_string(volumeCount) + "]"; // ddgi_* uniform-array element
                std::string num = std::to_string(volumeCount);              // ddgi_*Atlas<n> scalar sampler

                auto atlasSampler = samplers.find("ddgi_irradianceAtlas" + num);
                if (atlasSampler != samplers.end())
                    api.BindTextureUnit(atlasSampler->second.binding, probeManager->GetIrradianceAtlas(i)->GetHandle());

                auto distAtlasSampler = samplers.find("ddgi_distanceAtlas" + num);
                if (distAtlasSampler != samplers.end())
                    api.BindTextureUnit(distAtlasSampler->second.binding, probeManager->GetDistanceAtlas(i)->GetHandle());

                auto probeStateBuffer = probeManager->GetProbeStateBuffer(i);
                if (probeStateBuffer)
                    probeStateBuffer->Bind(14 + volumeCount);

                shader->SetVec3("ddgi_gridOrigin" + idx, probeManager->GetGridOrigin(i));
                shader->SetVec3("ddgi_gridSpacing" + idx, probeManager->GetGridSpacing(i));
                glm::ivec3 counts = probeManager->GetProbeCounts(i);
                shader->SetVec3("ddgi_probeCounts" + idx, glm::vec3(counts)); // ivec3 stored as vec3, see lit.frag
                shader->SetInt("ddgi_tileSize" + idx, (int)probeManager->GetTileSize(i));
                shader->SetInt("ddgi_atlasProbesPerRow" + idx, (int)probeManager->GetAtlasProbesPerRow(i));
                shader->SetInt("ddgi_atlasSize" + idx, (int)probeManager->GetAtlasSize(i));

                lastReady = i;
                volumeCount++;
            }

            for (int s = volumeCount; lastReady >= 0 && s < kMaxProbeVolumes; s++)
            {
                std::string num = std::to_string(s);

                auto atlasSampler = samplers.find("ddgi_irradianceAtlas" + num);
                if (atlasSampler != samplers.end())
                    api.BindTextureUnit(atlasSampler->second.binding, probeManager->GetIrradianceAtlas(lastReady)->GetHandle());

                auto distAtlasSampler = samplers.find("ddgi_distanceAtlas" + num);
                if (distAtlasSampler != samplers.end())
                    api.BindTextureUnit(distAtlasSampler->second.binding, probeManager->GetDistanceAtlas(lastReady)->GetHandle());

                auto padStateBuffer = probeManager->GetProbeStateBuffer(lastReady);
                if (padStateBuffer)
                    padStateBuffer->Bind(14 + s);
            }

            shader->SetInt("ddgi_volumeCount", volumeCount);
        }

        shader->SetInt("lightNB", studio ? currentView.studioLightCount : GetLightManager()->GetLightsCount());
        shader->SetVec3("camPos", currentView.position);
        auto lightCullingManager = GetLightCullingManager();
        if (studio)
        {
            // The studio rig has no point/spot lights : a single-cluster grid whose one (empty) entry
            // ImmediateRenderer binds in place of the scene's cluster buffers.
            shader->SetUVec2("clusterGridSizeXY", 1, 1);
            shader->SetFloat("clusterScaleZ", 0.0f);
            shader->SetFloat("clusterBiasZ", 0.0f);
        }
        else if (lightCullingManager)
        {
            shader->SetUVec2("clusterGridSizeXY", lightCullingManager->GetGridSizeX(), lightCullingManager->GetGridSizeY());
            shader->SetFloat("clusterScaleZ", lightCullingManager->GetClusterScaleZ());
            shader->SetFloat("clusterBiasZ", lightCullingManager->GetClusterBiasZ());
        }
    }

    void Renderer::BindMaterialScene(std::shared_ptr<Material> material)
    {
        if(material->GetRecieveShadows())
            GetShadowManager()->BindShadowMaps(material);
        GetSSAOManager()->BindSSAOTexture(material);
    }
}

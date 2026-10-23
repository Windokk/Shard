#include "renderer.hpp"
#include "engine/renderer/rhi/material/material.hpp"

#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"
#include "engine/renderer/features/lighting/light_manager.hpp"
#include "engine/renderer/features/lighting/shadow_manager.hpp"
#include "engine/renderer/features/lighting/probe_manager.hpp"
#include "engine/renderer/features/lighting/ssao_manager.hpp"
#include "engine/renderer/features/lighting/ssr_manager.hpp"
#include "engine/renderer/features/lighting/light_culling_manager.hpp"

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

        // How the shadows are filtered and biased (the project's rendering settings)
        if (m_ShadowManager)
        {
            const Projects::ShadowSettings& shadows = m_ShadowManager->GetSettings();
            shader->SetBool("shadowsEnabled", shadows.enabled);
            shader->SetInt("dirShadowFilterRadius", int(shadows.directionalFilterRadius));
            shader->SetFloat("dirShadowFilterSpread", shadows.directionalFilterSpread);
            shader->SetFloat("dirShadowNormalBias", shadows.directionalNormalBias);
            shader->SetFloat("dirShadowDepthBias", shadows.directionalDepthBias);
            shader->SetFloat("dirShadowSlopeBias", shadows.directionalSlopeBias);
            shader->SetInt("dirCascadeCount", int(shadows.CascadeCount()));
            for (uint32_t c = 0; c < shadows.CascadeCount(); ++c)
                shader->SetVec3("dirCascadeColors[" + std::to_string(c) + "]", shadows.cascadeColors[c]);
            shader->SetInt("spotShadowFilterRadius", int(shadows.spotFilterRadius));
            shader->SetFloat("spotShadowFilterSpread", shadows.spotFilterSpread);
            shader->SetFloat("spotShadowDepthBias", shadows.spotDepthBias);
            shader->SetFloat("spotShadowMinDepthBias", shadows.spotMinDepthBias);
            shader->SetInt("pointShadowFilterSamples", int(shadows.pointFilterSamples));
            shader->SetFloat("pointShadowFilterRadius", shadows.pointFilterRadius);
            shader->SetFloat("pointShadowDepthBias", shadows.pointDepthBias);
        }

        const EnvironmentProxy& environment = GetSnapshot().environment;

        RenderView currentView;
        GetCurrentView(currentView);
        const bool studio = currentView.lighting == ViewLighting::Studio;

        // A studio view is drawn straight into an 8 bit target, which the HDR scene pass is not : it maps the light itself
        shader->SetBool("outputDisplayColors", studio);

        if (studio)
        {
            // Fixed studio look : none of the world's lighting settings apply, and SSAO's texture is
            // the main viewport's (screen-space), which would be meaningless for another view.
            shader->SetVec3("ambientColor", glm::vec3(currentView.studioAmbient));
            shader->SetBool("ssaoEnabled", false);
        }
        else
        {
            shader->SetVec3("ambientColor", environment.ambientColor);

            shader->SetBool("ssaoEnabled", environment.ssaoEnabled);
            shader->SetFloat("ssaoIntensity", environment.ssaoIntensity);
        }

        if (!applyPassGlobals)
            return;

        // A studio view brings its own environment so its look never depends on the world.
        std::shared_ptr<EnvironmentMap> envMap;
        if (studio)
            envMap = currentView.studioEnvironment;
        else
            envMap = environment.skybox;

        if(envMap){
            //Bind skybox data
            const auto& samplers = shader->GetActiveSamplersMap();

            // A permutation that only reads the flag (the G-buffer's) has none of the environment's samplers
            auto bindIfUsed = [&](const char* name, const auto& texture)
            {
                auto sampler = samplers.find(name);
                if (sampler != samplers.end())
                    api.BindTextureUnit(sampler->second.binding, texture->GetHandle());
            };

            bindIfUsed("ibl_irradianceMap", envMap->GetIrradiance());
            bindIfUsed("ibl_prefilteredEnvMap", envMap->GetPrefilter());
            bindIfUsed("ibl_brdfLUT", envMap->GetBRDFLUT());
        }

        auto probeManager = GetProbeManager();
        bool ddgiReady = !studio && probeManager && probeManager->IsReady();
        shader->SetBool("ddgi_enabled", ddgiReady);

        if (ddgiReady)
        {
            int volumeCount = 0;
            int lastReady = -1;

            // The atlases are bindless handles (lit.frag has no sampler unit for them), low and high 32 bits
            auto setHandle = [&](const std::string& name, const std::shared_ptr<Texture2D>& atlas)
            {
                const uint64_t handle = atlas ? atlas->GetBindlessHandle() : 0;
                shader->SetUVec2(name, static_cast<uint32_t>(handle & 0xFFFFFFFFu), static_cast<uint32_t>(handle >> 32));
            };

            for (int i = 0; i < probeManager->GetActiveVolumeCount(); i++)
            {
                if (!probeManager->IsVolumeReady(i))
                    continue;

                std::string idx = "[" + std::to_string(volumeCount) + "]"; // ddgi_* uniform-array element

                setHandle("ddgi_irradianceAtlasHandle" + idx, probeManager->GetIrradianceAtlas(i));
                setHandle("ddgi_distanceAtlasHandle" + idx, probeManager->GetDistanceAtlas(i));

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
                std::string idx = "[" + std::to_string(s) + "]";

                setHandle("ddgi_irradianceAtlasHandle" + idx, probeManager->GetIrradianceAtlas(lastReady));
                setHandle("ddgi_distanceAtlasHandle" + idx, probeManager->GetDistanceAtlas(lastReady));

                auto padStateBuffer = probeManager->GetProbeStateBuffer(lastReady);
                if (padStateBuffer)
                    padStateBuffer->Bind(14 + s);
            }

            shader->SetInt("ddgi_volumeCount", volumeCount);
        }

        // Reflection probe : the one nearest to the view among those that have a baked picture (the camera inside a box is at distance 0, the
        // smaller box among those). One at a time : lit.frag reads a single probe (one sampler unit). A studio view brings its own environment,
        // so it never reflects the world's probes.
        {
            const ReflectionProbeProxy* best = nullptr;
            float bestDistance = 0.0f;
            float bestVolume = 0.0f;

            if (!studio)
            {
                for (const ReflectionProbeProxy& probe : GetSnapshot().reflectionProbes)
                {
                    if (!probe.volume.active || !probe.environment || !probe.environment->GetPrefilter())
                        continue;

                    const glm::vec3 outside = glm::max(glm::abs(currentView.position - probe.volume.center) - probe.volume.halfExtent, glm::vec3(0.0f));
                    const float distance = glm::length(outside);
                    const float volume = probe.volume.halfExtent.x * probe.volume.halfExtent.y * probe.volume.halfExtent.z;

                    if (!best || distance < bestDistance || (distance == bestDistance && volume < bestVolume))
                    {
                        best = &probe;
                        bestDistance = distance;
                        bestVolume = volume;
                    }
                }
            }

            shader->SetBool("rp_enabled", best != nullptr);

            if (best)
            {
                const auto& samplers = shader->GetActiveSamplersMap();
                auto sampler = samplers.find("rp_prefilter");
                if (sampler != samplers.end())
                    api.BindTextureUnit(sampler->second.binding, best->environment->GetPrefilter()->GetHandle());

                shader->SetVec3("rp_center", best->volume.center);
                shader->SetVec3("rp_halfExtent", best->volume.halfExtent);
                shader->SetInt("rp_shape", (int)best->shape);
                shader->SetVec3("rp_params", glm::vec3(best->intensity, best->blendDistance, best->boxProjection ? 1.0f : 0.0f));
            }
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

        // The studio views are drawn from another point of view than the screen's : the reflections, which are screen-space, mean nothing there
        bool screenSpaceView = true;
        if (HasViewOverride())
        {
            RenderView currentView;
            if (GetCurrentView(currentView))
                screenSpaceView = currentView.lighting != ViewLighting::Studio;
        }
        GetSSRManager()->BindSSR(material, screenSpaceView);
    }
}

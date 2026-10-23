#include "shadow_manager.hpp"

#include <algorithm>


#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/cubemap.hpp"
#include "engine/renderer/rhi/resources/texture/texture_array.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"

#include "engine/world/engine.hpp"


#include "engine/assets/resources_manager.hpp"

#include "engine/renderer/rhi/render_context.hpp"

#include "glm/gtx/string_cast.hpp"

namespace Shard::Engine::Rendering{

    static const std::string kPointClearPassName = "PointShadowClearPass";

    void ShadowManager::ComputeCascadeSplits(float* out, float cameraNear, float shadowFar) const
    {
        const uint32_t count = m_Settings.CascadeCount();
        for (uint32_t c = 0; c + 1 < count; ++c)
            out[c] = cameraNear + m_Settings.cascadeSplits[c] * (shadowFar - cameraNear);
        out[count - 1] = shadowFar;
    }

    void ShadowManager::Init(int pointShadowsResolution, int spotShadowsResolution, int dirShadowsResolution)
    {
        m_PointShadowsResolution = pointShadowsResolution;
        m_SpotShadowsResolution = spotShadowsResolution;
        m_DirShadowsResolution = dirShadowsResolution;

        m_DirShader = Core::GetEngine().GetResourcesManager()->Get<Rendering::Shader>(Core::Resources::AssetKind::Shader, "shaders/shadows/shadow_dir");
        m_PointShader = Core::GetEngine().GetResourcesManager()->Get<Rendering::Shader>(Core::Resources::AssetKind::Shader, "shaders/shadows/shadow_point");
        m_SpotShader = Core::GetEngine().GetResourcesManager()->Get<Rendering::Shader>(Core::Resources::AssetKind::Shader, "shaders/shadows/shadow_spot");
        m_ShadowMaps.clear();
        m_PointLightCount = 0;
        m_CurrentPointLightCapacity = 0;
    }

    float ShadowManager::GetDirectionalShadowFar(float cameraNear, float cameraFar) const
    {
        if (m_Settings.directionalDistance <= 0.0f)
            return cameraFar;
        return std::clamp(m_Settings.directionalDistance, cameraNear + 0.1f, std::max(cameraFar, cameraNear + 0.1f));
    }

    void ShadowManager::Configure(const Projects::ShadowSettings& settings)
    {
        const bool rebuild = int(settings.pointResolution) != m_PointShadowsResolution || int(settings.spotResolution) != m_SpotShadowsResolution ||
                             int(settings.directionalResolution) != m_DirShadowsResolution || settings.directionalDoubleSided != m_Settings.directionalDoubleSided ||
                             settings.directionalConservativeRaster != m_Settings.directionalConservativeRaster ||
                             settings.CascadeCount() != m_Settings.CascadeCount();

        m_Settings = settings;
        m_Settings.SanitizeCascades();
        m_PointShadowsResolution = int(settings.pointResolution);
        m_SpotShadowsResolution = int(settings.spotResolution);
        m_DirShadowsResolution = int(settings.directionalResolution);

        // The cascades follow the distance and the split on the next UpdatePassUniforms : the splits no longer match
        for (auto& sm : m_ShadowMaps)
            sm.second.splitCameraFar = -1.0f;

        if (!rebuild)
            return;

        // The maps (and the pipeline of the sun's pass, which holds the face culling) are made when a light registers : every light
        // registers again
        std::vector<std::pair<int, std::shared_ptr<LightData>>> lights;
        for (const auto& sm : m_ShadowMaps)
            if (sm.second.lightPtr)
                lights.emplace_back(sm.first, sm.second.lightPtr);

        for (const auto& light : lights)
            UnregisterLight(light.first);
        for (const auto& light : lights)
            RegisterOrUpdateLight(light.first, light.second);
    }

    void ShadowManager::UpdatePassUniforms()
    {
        const CameraProxy& cam = Core::GetEngine().GetRenderContext()->GetSnapshot().camera;

        // No camera yet (e.g. the player booting a world that has none): nothing to fit the cascades to.
        if(!cam.valid)
            return;

        for(auto& sm : m_ShadowMaps){

            if(sm.second.passes.empty())
                continue;

            if(sm.second.light.type == static_cast<int>(LightType::Directional))
            {
                // The cascades span the camera's depth range : the camera may have changed (or only appeared) since the light registered
                const float shadowFar = GetDirectionalShadowFar(cam.nearPlane, cam.farPlane);
                if(sm.second.splitCameraNear != cam.nearPlane || sm.second.splitCameraFar != shadowFar)
                {
                    ComputeCascadeSplits(sm.second.cascadeSplits, cam.nearPlane, shadowFar);
                    sm.second.splitCameraNear = cam.nearPlane;
                    sm.second.splitCameraFar = shadowFar;
                }

                for (uint32_t c = 0; c < sm.second.cascadeCount; ++c)
                {
                    auto pass = Core::GetEngine().GetRenderContext()->GetRenderPass(sm.second.passes[c]);

                    float splitNear = c == 0 ? cam.nearPlane : sm.second.cascadeSplits[c - 1];
                    float splitFar  = sm.second.cascadeSplits[c];
                    sm.second.lightMatrix[c] = sm.second.light.GetLightMatrix(cam.view, cam.fovDegrees, cam.AspectRatio(), splitNear, splitFar, m_DirShadowsResolution, -1.0f, m_Settings.stabilizeCascades, m_Settings.casterRange);

                    pass->customUniforms["lightSpaceMatrix"] = sm.second.lightMatrix[c];
                }
            }
            else if(sm.second.light.type == static_cast<int>(LightType::Spot)){
                auto pass = Core::GetEngine().GetRenderContext()->GetRenderPass(sm.second.passes[0]);
                sm.second.lightMatrix[0] = sm.second.light.GetLightMatrix(cam.view, -1, -1, -1, -1, -1, sm.second.light.outerCutoff);
                pass->customUniforms["lightSpaceMatrix"] = sm.second.lightMatrix[0];
            }
            else if(sm.second.light.type == static_cast<int>(LightType::Point)){
                float near = 0.1f;
                float far = sm.second.light.radius;
                glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, near, far);
                glm::vec3 pos = sm.second.light.position;

                sm.second.shadowMatrices[0] = proj * glm::lookAt(pos, pos + glm::vec3( 1,  0,  0), glm::vec3(0,  -1,  0)); // +X
                sm.second.shadowMatrices[1] = proj * glm::lookAt(pos, pos + glm::vec3(-1,  0,  0), glm::vec3(0,  -1,  0)); // -X
                sm.second.shadowMatrices[2] = proj * glm::lookAt(pos, pos + glm::vec3( 0,  1,  0), glm::vec3(0,  0,  1)); // +Y
                sm.second.shadowMatrices[3] = proj * glm::lookAt(pos, pos + glm::vec3( 0, -1,  0), glm::vec3(0,  0, -1)); // -Y
                sm.second.shadowMatrices[4] = proj * glm::lookAt(pos, pos + glm::vec3( 0,  0,  1), glm::vec3(0,  -1,  0)); // +Z
                sm.second.shadowMatrices[5] = proj * glm::lookAt(pos, pos + glm::vec3( 0,  0, -1), glm::vec3(0,  -1,  0)); // -Z
                
                auto pass = Core::GetEngine().GetRenderContext()->GetRenderPass(sm.second.passes[0]);

                static const std::string kShadowMatrixNames[6] = {"shadowMatrices[0]", "shadowMatrices[1]", "shadowMatrices[2]",
                                                                  "shadowMatrices[3]", "shadowMatrices[4]", "shadowMatrices[5]"};
                for(int i = 0; i < 6; i++)
                    pass->customUniforms[kShadowMatrixNames[i]] = sm.second.shadowMatrices[i];

                pass->customUniforms["farPlane"] = sm.second.light.radius;
                pass->customUniforms["lightPos"] = sm.second.light.position;
            }
        }
    }

    void ShadowManager::SubmitPasses(int lightIndex)
    {
        if (m_ShadowMaps.find(lightIndex) == m_ShadowMaps.end()){
            DEBUG_ERROR("Tried submitting shadow passes for a light, but this light was never registered.");
            return;
        }

        std::vector<std::shared_ptr<RenderPass>> passes = {};

        const CameraProxy& cam = Core::GetEngine().GetRenderContext()->GetSnapshot().camera;

        ShadowMap* sm = &m_ShadowMaps[lightIndex];
        LightData light = sm->light;

        if (light.type == static_cast<int>(LightType::Point))
        {
            PipelineSpecifications pointSpecs;
            pointSpecs.blending = false;
            pointSpecs.cullMode = CullMode::Back;
            pointSpecs.depthTest = true;
            pointSpecs.polygonMode = PolygonMode::Fill;
            pointSpecs.topology = PrimitiveTopology::Triangles;
            pointSpecs.shader = m_PointShader;
            pointSpecs.vertexLayout = {{{"aPos", ShaderDataType::Vec3, 0}}, sizeof(glm::vec3)};
            std::shared_ptr<Pipeline> pointShadowPassPipeline = Core::GetEngine().GetRenderContext()->GetOrAddPipeline(pointSpecs);
            
            std::shared_ptr<RenderPass> pass = std::make_shared<RenderPass>();
            pass->target = sm->framebuffer[0];
            pass->clearColor = false;
            // The target is the whole array, which kPointClearPassName clears once for every point light
            pass->clearDepth = false;
            pass->customPipeline = pointShadowPassPipeline;
            pass->overridePipeline = true;

            for(int i = 0; i < 6; i++)
                pass->customUniforms.emplace("shadowMatrices[" + std::to_string(i) + "]", sm->shadowMatrices[i]);

            pass->customUniforms.emplace("farPlane", light.radius);
            pass->customUniforms.emplace("lightPos", light.position);
            pass->customUniforms.emplace("cubeIndex", sm->cubeArrayLayer);

            pass->target->AttachCubemapArray(m_CubeArrayTex->GetHandle());

            pass->allowResize = false;
            pass->allowCulling = false;

            std::string passName = "ShadowPass_" + std::to_string(lightIndex) + "_0";
            sm->passes.push_back(passName);

            passes.push_back(pass);
        }
        else if(light.type == static_cast<int>(LightType::Spot))
        {
            PipelineSpecifications spotSpecs;
            spotSpecs.blending = false;
            spotSpecs.cullMode = CullMode::None;
            spotSpecs.depthTest = true;
            spotSpecs.polygonMode = PolygonMode::Fill;
            spotSpecs.topology = PrimitiveTopology::Triangles;
            spotSpecs.shader = m_SpotShader;
            spotSpecs.vertexLayout = {{{"aPos", ShaderDataType::Vec3, 0}}, sizeof(glm::vec3)};
            std::shared_ptr<Pipeline> spotShadowPassPipeline = Core::GetEngine().GetRenderContext()->GetOrAddPipeline(spotSpecs);

            std::shared_ptr<RenderPass> pass = std::make_shared<RenderPass>();
            pass->target = sm->framebuffer[0];
            pass->clearColor = false;
            pass->clearDepth = true;
            pass->customPipeline = spotShadowPassPipeline;
            pass->overridePipeline = true;
            pass->customUniforms.emplace("lightSpaceMatrix", sm->lightMatrix[0]);
            pass->allowResize = false;
            pass->allowCulling = false;
            pass->cullMatrixUniform = "lightSpaceMatrix";
            // Skipped while the light and every caster hold still (see RenderPass::cacheOutput).
            pass->cacheOutput = true;
            
            std::string passName = "ShadowPass_" + std::to_string(lightIndex) + "_0";
            sm->passes.push_back(passName);
            passes.push_back(pass);
        }
        else if(light.type == static_cast<int>(LightType::Directional))
        {
            PipelineSpecifications dirSpecs;
            dirSpecs.blending = false;
            // Both faces by default : a level is not made of closed volumes (a ceiling, a roof, a wall are often a single surface), and
            // drawing only the faces turned away from the sun leaves the edges of such surfaces open to it
            dirSpecs.cullMode = m_Settings.directionalDoubleSided ? CullMode::None : CullMode::Front;
            dirSpecs.conservativeRaster = m_Settings.directionalConservativeRaster;
            dirSpecs.depthTest = true;
            dirSpecs.polygonMode = PolygonMode::Fill;
            dirSpecs.topology = PrimitiveTopology::Triangles;
            dirSpecs.shader = m_DirShader;
            dirSpecs.vertexLayout = {{{"aPos", ShaderDataType::Vec3, 0}}, sizeof(glm::vec3)};

            std::shared_ptr<Pipeline> dirShadowPassPipeline = Core::GetEngine().GetRenderContext()->GetOrAddPipeline(dirSpecs);

            // The world can load before any camera is active (the player has none until play mode starts,
            // the editor builds its viewport camera after boot): fall back to Camera's default planes,
            // and leave the light matrices to UpdatePassUniforms() once a camera exists.
            const float camNear = cam.valid ? cam.nearPlane : 0.1f;
            const float camFar  = GetDirectionalShadowFar(camNear, cam.valid ? cam.farPlane : 100.0f);
            sm->splitCameraNear = cam.valid ? cam.nearPlane : -1.0f;
            sm->splitCameraFar = cam.valid ? camFar : -1.0f;

            ComputeCascadeSplits(sm->cascadeSplits, camNear, camFar);

            for (uint32_t c = 0; c < sm->cascadeCount; ++c)
            {
                std::shared_ptr<RenderPass> pass = std::make_shared<RenderPass>();
                pass->target = sm->framebuffer[c];
                pass->clearColor = false;
                pass->clearDepth = true;
                pass->customPipeline = dirShadowPassPipeline;
                pass->overridePipeline = true;

                if(cam.valid){
                    float splitNear = c == 0 ? cam.nearPlane : sm->cascadeSplits[c - 1];
                    float splitFar  = sm->cascadeSplits[c];
                    sm->lightMatrix[c] = light.GetLightMatrix(cam.view, cam.fovDegrees, cam.AspectRatio(), splitNear, splitFar, m_DirShadowsResolution, -1.0f, m_Settings.stabilizeCascades, m_Settings.casterRange);
                }
                else{
                    sm->lightMatrix[c] = glm::mat4(1.0f);
                }

                pass->customUniforms.emplace("lightSpaceMatrix", sm->lightMatrix[c]);

                pass->allowResize = false;
                pass->allowCulling = false;
                pass->cullMatrixUniform = "lightSpaceMatrix";
                // Skipped while the light, the camera fit and every caster hold still (see RenderPass::cacheOutput).
                pass->cacheOutput = true;

                std::string passName = "ShadowPass_" + std::to_string(lightIndex) + "_" + std::to_string(passes.size());
                sm->passes.push_back(passName); 

                passes.push_back(pass);
            }
        }
        
        for(size_t i = 0; i < passes.size(); i++){
            std::string passName = "ShadowPass_" + std::to_string(lightIndex) + "_" + std::to_string(i);

            passes[i]->externalDrawList = Core::GetEngine().GetRenderContext()->GetShadowDrawList();

            std::vector<std::string> dependencies;
            if (light.type == static_cast<int>(LightType::Point))
                dependencies.push_back(kPointClearPassName);

            Core::GetEngine().GetRenderContext()->AddRenderPass(passes[i], passName, dependencies);
            
            // Add dependency: ForwardPass depends on this shadow pass
            Core::GetEngine().GetRenderContext()->AddDependencyToPass("ForwardPass", passName);
        }
    }

    void ShadowManager::EnsureCubeArrayCapacity(int requiredPointLights)
    {
        if (requiredPointLights <= m_CurrentPointLightCapacity)
            return; // we already have enough layers

        // Grown geometrically : every growth recreates the array (and re-renders every point light's shadow into it)
        RecreateCubeArray(std::max(requiredPointLights, m_CurrentPointLightCapacity * 2));
    }

    void ShadowManager::RecreateCubeArray(int capacity)
    {
        m_CubeArrayTex.reset();
        m_CurrentPointLightCapacity = capacity;

        if (capacity > 0)
        {
            TextureSpecifications texSpecs;

            texSpecs.internalFormat = TextureInternalFormat::Depth32F;
            texSpecs.format = TextureFormat::Depth;
            texSpecs.compareMode = TextureCompareMode::None;
            texSpecs.compareFunc = TextureCompareFunc::Less;
            texSpecs.width = m_PointShadowsResolution;
            texSpecs.height = m_PointShadowsResolution;
            texSpecs.minFilter = TextureFilter::Linear;
            texSpecs.magFilter = TextureFilter::Linear;
            texSpecs.wrapR = TextureWrap::ClampEdge;
            texSpecs.wrapS = TextureWrap::ClampEdge;
            texSpecs.wrapT = TextureWrap::ClampEdge;

            std::vector<std::array<unsigned char *, 6>> emptyData(capacity, {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr});
            m_CubeArrayTex = CubemapArray::Create(texSpecs, emptyData);

            AddPointClearPass();
        }
        else
        {
            RemovePointClearPass();
        }

        SyncPointShadowTargets();
    }

    void ShadowManager::AddPointClearPass()
    {
        if (m_PointClearTarget)
            return;

        FramebufferSpecifications specs{};
        specs.width  = m_PointShadowsResolution;
        specs.height = m_PointShadowsResolution;
        specs.hasColor = false;
        specs.hasDepth = false;
        specs.multisampled = false;
        specs.depthSpecs.internalFormat = TextureInternalFormat::Depth32F;
        m_PointClearTarget = Framebuffer::Create(specs);

        // Draws nothing : its own (empty) draw list, only the clear of the whole array
        std::shared_ptr<RenderPass> pass = std::make_shared<RenderPass>();
        pass->target = m_PointClearTarget;
        pass->clearColor = false;
        pass->clearDepth = true;
        pass->allowResize = false;
        pass->allowCulling = false;

        Core::GetEngine().GetRenderContext()->AddRenderPass(pass, kPointClearPassName, {});
    }

    void ShadowManager::RemovePointClearPass()
    {
        if (!m_PointClearTarget)
            return;

        Core::GetEngine().GetRenderContext()->RemoveRenderPass(kPointClearPassName);
        if (m_PointClearTarget->IsValid())
            m_PointClearTarget->Destroy();
        m_PointClearTarget.reset();
    }

    void ShadowManager::SyncPointShadowTargets()
    {
        const uint32_t handle = m_CubeArrayTex ? m_CubeArrayTex->GetHandle() : 0;

        if (m_PointClearTarget && m_PointClearTarget->IsValid())
            m_PointClearTarget->AttachCubemapArray(handle);

        for (auto& [lightIndex, sm] : m_ShadowMaps)
        {
            if (sm.light.type != (int)LightType::Point)
                continue;

            if (sm.framebuffer[0] && sm.framebuffer[0]->IsValid())
                sm.framebuffer[0]->AttachCubemapArray(handle);

            if (sm.passes.empty())
                continue;

            if (std::shared_ptr<RenderPass> pass = Core::GetEngine().GetRenderContext()->GetRenderPass(sm.passes[0]))
                pass->customUniforms["cubeIndex"] = sm.cubeArrayLayer;
        }
    }

    void ShadowManager::RegisterOrUpdateLight(int lightIndex, std::shared_ptr<LightData> light)
    {
        // Early exit: no shadow
        if (!light->castShadow) {
            if (m_ShadowMaps.find(lightIndex) != m_ShadowMaps.end())
                UnregisterLight(lightIndex);
            return;
        }

        // Ensure ShadowMap exists
        auto [it, inserted] = m_ShadowMaps.try_emplace(lightIndex);
        ShadowMap& sm = it->second;

        // Type tracking
        int currentType  = light->type;
        int previousType = m_PreviousLightTypes.find(lightIndex) != m_PreviousLightTypes.end() ? m_PreviousLightTypes[lightIndex] : -1;

        // Always update light
        sm.light = *light;
        sm.lightPtr = light;

        bool needsRebuild = (previousType != currentType);

        // Rebuilding of shadow maps (only if type changed)
        if (needsRebuild)
        {
            // Cleanup of old passes
            if (!sm.passes.empty()) {

                for (const auto& passName : sm.passes)
                    Core::GetEngine().GetRenderContext()->RemoveRenderPass(passName);

                sm.passes.clear();
            }

            // Cleanup of old GPU resources
            switch (previousType)
            {
                case (int)LightType::Directional:
                    for (uint32_t c = 0; c < sm.cascadeCount; ++c)
                        if (sm.framebuffer[c] && sm.framebuffer[c]->IsValid())
                            sm.framebuffer[c]->Destroy();
                    break;

                case (int)LightType::Spot:
                    if (sm.framebuffer[0] && sm.framebuffer[0]->IsValid())
                        sm.framebuffer[0]->Destroy();
                    break;

                case (int)LightType::Point:
                {
                    if (sm.framebuffer[0] && sm.framebuffer[0]->IsValid())
                        sm.framebuffer[0]->Destroy();

                    int removedLayer = sm.cubeArrayLayer;

                    for (auto& other : m_ShadowMaps) {
                        if (other.second.light.type == (int)LightType::Point &&
                            other.second.cubeArrayLayer > removedLayer)
                        {
                            --other.second.cubeArrayLayer;
                        }
                    }

                    --m_PointLightCount;
                    TryShrinkCubeArray();
                    SyncPointShadowTargets();
                    break;
                }
            }

            // The framebuffers of a sun or a spot draw into layers of the arrays SyncLayeredShadowTargets points them at
            sm.attachedArray = 0;
            sm.attachedBaseLayer = -2;

            // New resources creation
            switch (currentType)
            {
                case (int)LightType::Point:
                {
                    EnsureCubeArrayCapacity(m_PointLightCount + 1);

                    sm.cubeArrayLayer = m_PointLightCount;
                    sm.resolution = m_PointShadowsResolution;

                    float near = 0.1f;
                    float far  = light->radius;

                    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, near, far);
                    glm::vec3 pos  = light->position;

                    sm.shadowMatrices[0] = proj * glm::lookAt(pos, pos + glm::vec3( 1,  0,  0), glm::vec3(0,  -1,  0)); // +X
                    sm.shadowMatrices[1] = proj * glm::lookAt(pos, pos + glm::vec3(-1,  0,  0), glm::vec3(0,  -1,  0)); // -X
                    sm.shadowMatrices[2] = proj * glm::lookAt(pos, pos + glm::vec3( 0,  1,  0), glm::vec3(0,  0,  1)); // +Y
                    sm.shadowMatrices[3] = proj * glm::lookAt(pos, pos + glm::vec3( 0, -1,  0), glm::vec3(0,  0, -1)); // -Y
                    sm.shadowMatrices[4] = proj * glm::lookAt(pos, pos + glm::vec3( 0,  0,  1), glm::vec3(0,  -1,  0)); // +Z
                    sm.shadowMatrices[5] = proj * glm::lookAt(pos, pos + glm::vec3( 0,  0, -1), glm::vec3(0,  -1,  0)); // -Z

                    FramebufferSpecifications specs{};
                    specs.width  = m_PointShadowsResolution;
                    specs.height = m_PointShadowsResolution;
                    specs.hasColor = false;
                    specs.hasDepth = false;
                    specs.multisampled = false;
                    specs.depthSpecs.internalFormat = TextureInternalFormat::Depth32F;

                    sm.framebuffer[0] = Framebuffer::Create(specs);
                    sm.framebuffer[0]->AttachCubemapArray(m_CubeArrayTex->GetHandle());

                    ++m_PointLightCount;
                    break;
                }

                case (int)LightType::Directional:
                {
                    sm.resolution = m_DirShadowsResolution;

                    FramebufferSpecifications specs{};
                    specs.width  = m_DirShadowsResolution;
                    specs.height = m_DirShadowsResolution;
                    specs.hasDepth = false;
                    specs.hasColor = false;
                    specs.multisampled = false;
                    specs.depthSpecs.internalFormat = TextureInternalFormat::Depth32F;

                    sm.cascadeCount = m_Settings.CascadeCount();
                    for (uint32_t c = 0; c < sm.cascadeCount; ++c) {
                        sm.framebuffer[c] = Framebuffer::Create(specs);
                    }
                    break;
                }

                case (int)LightType::Spot:
                {
                    sm.resolution = m_SpotShadowsResolution;

                    FramebufferSpecifications specs{};
                    specs.width  = m_SpotShadowsResolution;
                    specs.height = m_SpotShadowsResolution;
                    specs.hasDepth = false;
                    specs.hasColor = false;
                    specs.multisampled = false;
                    specs.depthSpecs.internalFormat = TextureInternalFormat::Depth32F;

                    sm.framebuffer[0] = Framebuffer::Create(specs);
                    sm.lightMatrix[0] = light->GetLightMatrix(glm::mat4(1.0f), -1, -1, -1, -1, -1, sm.light.outerCutoff);
                    break;
                }
            }

            m_PreviousLightTypes[lightIndex] = currentType;
        }

        // If we did rebuilt gpu resources, re-submit passes
        if (needsRebuild)
            SubmitPasses(lightIndex);

        // The set of registered shadow casters may have changed shape (new light, type change) -
        // recompute every light's shadowIndex so it's correct before the caller (LightManager::Update)
        // uploads the SSBO.
        ReassignShadowIndices();
    }

    void ShadowManager::ReassignShadowIndices()
    {
        int dirIndex = 0, spotIndex = 0;

        for (auto& [lightIndex, sm] : m_ShadowMaps)
        {
            if (!sm.lightPtr)
                continue;

            switch (sm.light.type)
            {
                case (int)LightType::Directional:
                    // lit.frag has NUM_CASCADES cascade slots, each sun takes one per cascade : a sun beyond them would read past
                    sm.lightPtr->shadowIndex = (uint32_t(dirIndex + 1) * m_Settings.CascadeCount() <= NUM_CASCADES) ? dirIndex : -1;
                    ++dirIndex;
                    break;

                case (int)LightType::Spot:
                    sm.lightPtr->shadowIndex = (spotIndex < MAX_SPOT_LIGHTS) ? spotIndex : -1;
                    ++spotIndex;
                    break;

                case (int)LightType::Point:
                    sm.lightPtr->shadowIndex = (sm.cubeArrayLayer < MAX_POINT_LIGHTS) ? sm.cubeArrayLayer : -1;
                    break;
            }
        }

        SyncLayeredShadowTargets();
    }

    std::shared_ptr<Texture2DArray> ShadowManager::CreateShadowArray(int resolution, uint32_t layers, bool compare) const
    {
        TextureSpecifications specs;
        specs.width = uint32_t(resolution);
        specs.height = uint32_t(resolution);
        specs.borderColor = COL_RGBA(1.0f);
        specs.compareMode = compare ? TextureCompareMode::CompareRefToTexture : TextureCompareMode::None;
        specs.compareFunc = TextureCompareFunc::LessOrEqual;
        specs.minFilter = TextureFilter::Linear;
        specs.magFilter = TextureFilter::Linear;
        specs.wrapS = TextureWrap::ClampBorder;
        specs.wrapT = TextureWrap::ClampBorder;
        specs.wrapR = TextureWrap::ClampBorder;
        specs.internalFormat = TextureInternalFormat::Depth32F;
        specs.format = TextureFormat::Depth;
        specs.generateMips = false;

        return Texture2DArray::Create(specs, layers);
    }

    void ShadowManager::SyncLayeredShadowTargets()
    {
        // The layers the lights that have a slot need : a sun takes one per cascade
        uint32_t dirLayers = 0, spotLayers = 0;
        for (const auto& [lightIndex, sm] : m_ShadowMaps)
        {
            if (!sm.lightPtr || sm.lightPtr->shadowIndex < 0)
                continue;

            const uint32_t slot = uint32_t(sm.lightPtr->shadowIndex);
            if (sm.light.type == (int)LightType::Directional)
                dirLayers = std::max(dirLayers, (slot + 1) * sm.cascadeCount);
            else if (sm.light.type == (int)LightType::Spot)
                spotLayers = std::max(spotLayers, slot + 1);
        }

        // An array is made again when it lacks layers or has the wrong size (the resolution changed), and let go of when no light
        // needs it. The suns' never has more than NUM_CASCADES layers : it is made exactly as big. The spots' grows geometrically,
        // since each growth throws away (and has every spot draw again) the maps it held.
        bool recreated = false;
        auto fit = [&](std::shared_ptr<Texture2DArray>& array, uint32_t needed, uint32_t capacity, int resolution, bool compare)
        {
            if (needed == 0)
            {
                recreated |= array != nullptr;
                array.reset();
                return;
            }

            if (array && array->GetLayerCount() >= needed && array->GetWidth() == uint32_t(resolution))
                return;

            array = CreateShadowArray(resolution, capacity, compare);
            recreated = true;
        };

        const uint32_t spotCapacity = std::min<uint32_t>(MAX_SPOT_LIGHTS, std::max(spotLayers, m_SpotArrayTex ? m_SpotArrayTex->GetLayerCount() * 2 : 1u));
        fit(m_DirArrayTex, dirLayers, dirLayers, m_DirShadowsResolution, true);
        fit(m_SpotArrayTex, spotLayers, spotCapacity, m_SpotShadowsResolution, false);

        for (auto& [lightIndex, sm] : m_ShadowMaps)
        {
            const bool directional = sm.light.type == (int)LightType::Directional;
            if (!directional && sm.light.type != (int)LightType::Spot)
                continue;

            const std::shared_ptr<Texture2DArray>& array = directional ? m_DirArrayTex : m_SpotArrayTex;
            const uint32_t count = directional ? sm.cascadeCount : 1u;
            const bool hasSlot = sm.lightPtr && sm.lightPtr->shadowIndex >= 0 && array;

            const uint32_t wantedArray = hasSlot ? array->GetHandle() : 0;
            const int wantedBase = hasSlot ? sm.lightPtr->shadowIndex * int(count) : -1;

            if (!recreated && wantedArray == sm.attachedArray && wantedBase == sm.attachedBaseLayer)
                continue;

            sm.attachedArray = wantedArray;
            sm.attachedBaseLayer = wantedBase;

            for (uint32_t c = 0; c < count; ++c)
            {
                if (sm.framebuffer[c] && sm.framebuffer[c]->IsValid())
                    sm.framebuffer[c]->AttachDepthLayer(wantedArray, hasSlot ? uint32_t(wantedBase) + c : 0u);

                if (c >= sm.passes.size())
                    continue;

                // A light without a slot draws nowhere (and lit.frag never reads it) ; the others draw their new layer on the next frame
                if (std::shared_ptr<RenderPass> pass = Core::GetEngine().GetRenderContext()->GetRenderPass(sm.passes[c]))
                {
                    pass->enabled = hasSlot;
                    pass->hasCachedState = false;
                }
            }
        }
    }

    void ShadowManager::TryShrinkCubeArray()
    {
        if (m_PointLightCount == 0) {
            if (m_CurrentPointLightCapacity != 0 || m_CubeArrayTex)
                RecreateCubeArray(0);
            return;
        }

        // Only shrink if capacity is significantly larger than usage
        if (m_PointLightCount >= m_CurrentPointLightCapacity / 2 || m_CurrentPointLightCapacity <= 4)
            return;

        RecreateCubeArray(std::max(4, m_PointLightCount)); // never shrink below 4 layers
    }

    void ShadowManager::UnregisterLight(int lightIndex)
    {
        if (m_ShadowMaps.find(lightIndex) == m_ShadowMaps.end()){
            DEBUG_ERROR("Tried unregistering a light from shadow mappings, but this light was never registered.");
            return;
        }

        ShadowMap& removedSM = m_ShadowMaps[lightIndex];

        // ---- Clean up Directional Light Resources (multiple cascades) ----
        if (removedSM.light.type == static_cast<int>(LightType::Directional)) {
            for (uint32_t c = 0; c < removedSM.cascadeCount; ++c) {
                if (removedSM.framebuffer[c] && removedSM.framebuffer[c]->IsValid()){
                    removedSM.framebuffer[c]->Destroy();
                    removedSM.framebuffer[c].reset();
                }
            }
        }

        // ---- Clean up Spot Light Resources (single map) ----
        else if (removedSM.light.type == static_cast<int>(LightType::Spot)) {
            if (removedSM.framebuffer[0]->IsValid()){
                removedSM.framebuffer[0]->Destroy();
                removedSM.framebuffer[0].reset();
            }
        }

        // ---- Special Case: Point Light (cube map layer) ----
        else if (removedSM.light.type == static_cast<int>(LightType::Point)) {
            int removedLayer = removedSM.cubeArrayLayer;

            // Shift down cube array layer indices for other point lights
            for (auto& sm : m_ShadowMaps) {
                if (sm.second.light.type == static_cast<int>(LightType::Point) &&
                    sm.second.cubeArrayLayer > removedLayer) {
                    --sm.second.cubeArrayLayer;
                }
            }

            --m_PointLightCount;

            if (removedSM.framebuffer[0] && removedSM.framebuffer[0]->IsValid()) {
                removedSM.framebuffer[0]->Destroy();
                removedSM.framebuffer[0].reset();
            }

            // The remaining point lights' passes must write (and lit.frag read) their shifted layers
            TryShrinkCubeArray();
            SyncPointShadowTargets();
        }

        for(std::string passName : removedSM.passes){
            Core::GetEngine().GetRenderContext()->RemoveRenderPass(passName);
        }

        // No longer a registered shadow caster - clear its slot before it drops out of m_ShadowMaps.
        if (removedSM.lightPtr)
            removedSM.lightPtr->shadowIndex = -1;

        // ---- Remove from list ----
        m_ShadowMaps.erase(lightIndex);
        m_PreviousLightTypes.erase(lightIndex);

        // Remaining same-type lights may need to shift into the slot this one vacated.
        ReassignShadowIndices();
    }

    void ShadowManager::BindShadowMaps(std::shared_ptr<Shard::Engine::Rendering::Material> material)
    {
        // Every slot bound here is read from LightData::shadowIndex (kept in sync by
        // ReassignShadowIndices, called after every registration/removal) rather than recomputed by
        // scanning order here - that index is what lit.frag's `l.shadowIndex` uses to look up the same
        // slot, and the two must agree regardless of what order/subset of lights a fragment iterates
        // (which, under clustered light culling, is no longer the full-array scan order).
        for (const auto& sm : m_ShadowMaps)
        {
            const LightData& light = sm.second.light;

            if (!light.castShadow || !sm.second.lightPtr)
                continue;

            int shadowIndex = sm.second.lightPtr->shadowIndex;
            if (shadowIndex < 0)
                continue;

            // Directional lights
            if (light.type == Directional)
            {
                if (!sm.second.passes.empty())
                {
                    int base = shadowIndex * int(sm.second.cascadeCount);

                    for (uint32_t c = 0; c < sm.second.cascadeCount; ++c)
                    {
                        material->SetScalarParameter(
                            "dirLightSpaceMatrices[" + std::to_string(base + c) + "]",
                            sm.second.lightMatrix[c]
                        );

                        material->SetScalarParameter(
                            "dirCascadeSplits[" + std::to_string(base + c) + "]",
                            sm.second.cascadeSplits[c]
                        );
                    }
                }
            }
            // Spot lights
            else if (light.type == static_cast<int>(LightType::Spot))
            {
                material->SetScalarParameter(
                    "spotLightSpaceMatrices[" + std::to_string(shadowIndex) + "]",
                    sm.second.lightMatrix[0]
                );
            }
            // Point lights
            else if (light.type == static_cast<int>(LightType::Point))
            {
                material->SetScalarParameter(
                    "pointLightFarPlanes[" + std::to_string(shadowIndex) + "]",
                    light.radius
                );
            }
        }

        // One sampler for each kind of light : the layer a light reads is its slot (and its cascade), see SyncLayeredShadowTargets
        // (0 when there is none : the material must not keep the name of an array that was let go of)
        material->SetTextureParameter("dirShadowMapArray", m_DirArrayTex ? m_DirArrayTex->GetHandle() : 0);
        material->SetTextureParameter("spotShadowMapArray", m_SpotArrayTex ? m_SpotArrayTex->GetHandle() : 0);
        if(m_CurrentPointLightCapacity && m_CubeArrayTex)
            material->SetTextureParameter("pointShadowMapArray", m_CubeArrayTex->GetHandle());
    }

    void ShadowManager::ClearAll()
    {
        // Properly tear down every shadow map's render passes and GPU resources, and
        // reset all bookkeeping (previous light types, point light count/capacity).
        //
        // This matters because callers (e.g. LightManager::RemoveLight) clear all shadow
        // maps and then re-register every remaining light under its (possibly shifted)
        // index. If m_PreviousLightTypes isn't reset too, RegisterOrUpdateLight sees the
        // same type at that index as before and skips rebuilding (needsRebuild == false),
        // leaving a freshly default-constructed ShadowMap with null framebuffers and
        // uninitialized light matrices wired into rendering.
        for (auto& entry : m_ShadowMaps)
        {
            ShadowMap& sm = entry.second;

            for (const auto& passName : sm.passes)
                Core::GetEngine().GetRenderContext()->RemoveRenderPass(passName);

            switch (sm.light.type)
            {
                case (int)LightType::Directional:
                    for (uint32_t c = 0; c < sm.cascadeCount; ++c)
                        if (sm.framebuffer[c] && sm.framebuffer[c]->IsValid())
                            sm.framebuffer[c]->Destroy();
                    break;

                case (int)LightType::Spot:
                case (int)LightType::Point:
                    if (sm.framebuffer[0] && sm.framebuffer[0]->IsValid())
                        sm.framebuffer[0]->Destroy();
                    break;
            }
        }

        m_ShadowMaps.clear();
        m_PreviousLightTypes.clear();

        RemovePointClearPass();

        m_PointLightCount = 0;
        m_CurrentPointLightCapacity = 0;
        m_CubeArrayTex.reset();
        m_DirArrayTex.reset();
        m_SpotArrayTex.reset();
    }
}
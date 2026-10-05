#include "shadow_manager.hpp"

#include "engine/renderer/components/camera.hpp"

#include "engine/renderer/material/shader.hpp"
#include "engine/renderer/material/material.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/cubemap.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"

#include "engine/core/engine.hpp"

#include "engine/renderer/frontend/camera_manager.hpp"

#include "engine/core/resources/resources_manager.hpp"

#include "engine/renderer/frontend/renderer.hpp"

#include "glm/gtx/string_cast.hpp"

namespace Shard::Engine::Rendering{

    float ComputeCascadeSplitDistance(int cascadeIndex, float nearPlane, float farPlane, int totalCascades)
    {
        float nd = static_cast<float>(cascadeIndex + 1) / static_cast<float>(totalCascades);

        // Uniform split
        float logSplit = nearPlane * std::pow(farPlane / nearPlane, nd);

        float uniformSplit = nearPlane + (farPlane - nearPlane) * nd;

        float splitDist = 0.2f * logSplit + (1 - 0.2f) * uniformSplit;

        return splitDist;
    }

    void ShadowManager::Init(int pointShadowsResolution, int spotShadowsResolution, int dirShadowsResolution)
    {
        m_PointShadowsResolution = pointShadowsResolution;
        m_SpotShadowsResolution = spotShadowsResolution;
        m_DirShadowsResolution = dirShadowsResolution;

        m_DirShader = Core::GetEngine().GetResourcesManager()->GetShader("shaders/shadows/shadow_dir");
        m_PointShader = Core::GetEngine().GetResourcesManager()->GetShader("shaders/shadows/shadow_point");
        m_SpotShader = Core::GetEngine().GetResourcesManager()->GetShader("shaders/shadows/shadow_spot");
        m_ShadowMaps.clear();
        m_PointLightCount = 0;
        m_CurrentPointLightCapacity = 0;
    }

    void ShadowManager::UpdatePassUniforms()
    {
        std::shared_ptr<Objects::Components::Camera> cam = Core::GetEngine().GetCameraManager()->GetActiveCamera();

        for(auto& sm : m_ShadowMaps){

            if(sm.second.passes.empty())
                continue;

            if(sm.second.light.type == static_cast<int>(LightType::Directional))
            {
                for (int c = 0; c < CASCADES_PER_LIGHT; ++c)
                {
                    auto pass = Core::GetEngine().GetRenderer()->GetRenderPass(sm.second.passes[c]);

                    float splitNear = c == 0 ? cam->nearPlane : sm.second.cascadeSplits[c - 1];
                    float splitFar  = sm.second.cascadeSplits[c];
                    sm.second.lightMatrix[c] = sm.second.light.GetLightMatrix(cam->GetView(), *cam->GetFOV(), cam->GetSize().x/cam->GetSize().y, splitNear, splitFar, m_DirShadowsResolution);

                    pass->customUniforms["lightSpaceMatrix"] = sm.second.lightMatrix[c];
                }
            }
            else if(sm.second.light.type == static_cast<int>(LightType::Spot)){
                auto pass = Core::GetEngine().GetRenderer()->GetRenderPass(sm.second.passes[0]);
                sm.second.lightMatrix[0] = sm.second.light.GetLightMatrix(cam->GetView(), -1, -1, -1, -1, -1, sm.second.light.outerCutoff);
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
                
                auto pass = Core::GetEngine().GetRenderer()->GetRenderPass(sm.second.passes[0]);

                for(int i = 0; i < 6; i++)
                    pass->customUniforms["shadowMatrices[" + std::to_string(i) + "]"] = sm.second.shadowMatrices[i];

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

        std::shared_ptr<Objects::Components::Camera> cam = Core::GetEngine().GetCameraManager()->GetActiveCamera();

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
            std::shared_ptr<Pipeline> pointShadowPassPipeline = Core::GetEngine().GetRenderer()->GetOrAddPipeline(pointSpecs);
            
            std::shared_ptr<RenderPass> pass = std::make_shared<RenderPass>();
            pass->target = sm->framebuffer[0];
            pass->clearColor = false;
            pass->clearDepth = true;
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
            std::shared_ptr<Pipeline> spotShadowPassPipeline = Core::GetEngine().GetRenderer()->GetOrAddPipeline(spotSpecs);

            std::shared_ptr<RenderPass> pass = std::make_shared<RenderPass>();
            pass->target = sm->framebuffer[0];
            pass->clearColor = false;
            pass->clearDepth = true;
            pass->customPipeline = spotShadowPassPipeline;
            pass->overridePipeline = true;
            pass->customUniforms.emplace("lightSpaceMatrix", sm->lightMatrix[0]);
            pass->allowResize = false;
            pass->allowCulling = false;
            
            std::string passName = "ShadowPass_" + std::to_string(lightIndex) + "_0";
            sm->passes.push_back(passName);
            passes.push_back(pass);
        }
        else if(light.type == static_cast<int>(LightType::Directional))
        {
            PipelineSpecifications dirSpecs;
            dirSpecs.blending = false;
            dirSpecs.cullMode = CullMode::Front;
            dirSpecs.depthTest = true;
            dirSpecs.polygonMode = PolygonMode::Fill;
            dirSpecs.topology = PrimitiveTopology::Triangles;
            dirSpecs.shader = m_DirShader;
            dirSpecs.vertexLayout = {{{"aPos", ShaderDataType::Vec3, 0}}, sizeof(glm::vec3)};

            std::shared_ptr<Pipeline> dirShadowPassPipeline = Core::GetEngine().GetRenderer()->GetOrAddPipeline(dirSpecs);

            for (int c = 0; c < CASCADES_PER_LIGHT; ++c)
                sm->cascadeSplits[c] = ComputeCascadeSplitDistance(c, cam->nearPlane, cam->farPlane, CASCADES_PER_LIGHT);

            for (int c = 0; c < CASCADES_PER_LIGHT; ++c)
            {
                std::shared_ptr<RenderPass> pass = std::make_shared<RenderPass>();
                pass->target = sm->framebuffer[c];
                pass->clearColor = false;
                pass->clearDepth = true;
                pass->customPipeline = dirShadowPassPipeline;
                pass->overridePipeline = true;

                float splitNear = c == 0 ? cam->nearPlane : sm->cascadeSplits[c - 1];
                float splitFar  = sm->cascadeSplits[c];
                sm->lightMatrix[c] = light.GetLightMatrix(cam->GetView(), *cam->GetFOV(), cam->GetSize().x/cam->GetSize().y, splitNear, splitFar, m_DirShadowsResolution);

                pass->customUniforms.emplace("lightSpaceMatrix", sm->lightMatrix[c]);

                pass->allowResize = false;
                pass->allowCulling = false;

                std::string passName = "ShadowPass_" + std::to_string(lightIndex) + "_" + std::to_string(passes.size());
                sm->passes.push_back(passName); 

                passes.push_back(pass);
            }
        }
        
        for(int i = 0; i < passes.size(); i++){
            std::string passName = "ShadowPass_" + std::to_string(lightIndex) + "_" + std::to_string(i);

            passes[i]->externalDrawList = Core::GetEngine().GetRenderer()->GetShadowDrawList();

            Core::GetEngine().GetRenderer()->AddRenderPass(passes[i], passName, {});
            
            // Add dependency: ForwardPass depends on this shadow pass
            Core::GetEngine().GetRenderer()->AddDependencyToPass("ForwardPass", passName);
        }
    }

    void ShadowManager::EnsureCubeArrayCapacity(int requiredPointLights)
    {
        if (requiredPointLights <= m_CurrentPointLightCapacity)
            return; // we already have enough layers

        // Delete old array if it exists
        if(m_CubeArrayTex){
            if (m_CubeArrayTex->IsValid())
                m_CubeArrayTex.reset();
        }

        m_CurrentPointLightCapacity = requiredPointLights;

        TextureSpecifications texSpecs;

        texSpecs.internalFormat = TextureInternalFormat::Depth32F;
        texSpecs.compareMode = TextureCompareMode::None;
        texSpecs.compareFunc = TextureCompareFunc::Less;
        texSpecs.width = m_PointShadowsResolution;
        texSpecs.height = m_PointShadowsResolution;
        texSpecs.minFilter = TextureFilter::Linear;
        texSpecs.magFilter = TextureFilter::Linear;
        texSpecs.wrapR = TextureWrap::ClampEdge;
        texSpecs.wrapS = TextureWrap::ClampEdge;
        texSpecs.wrapT = TextureWrap::ClampEdge;

        std::vector<std::array<unsigned char *, 6>> emptyData;
        for(int i = 0; i < m_CurrentPointLightCapacity; i++){
            emptyData.push_back({nullptr, nullptr, nullptr, nullptr, nullptr, nullptr});
        }

        m_CubeArrayTex = CubemapArray::Create(texSpecs, emptyData);
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
                    Core::GetEngine().GetRenderer()->RemoveRenderPass(passName);

                sm.passes.clear();
            }

            // Cleanup of old GPU resources
            switch (previousType)
            {
                case (int)LightType::Directional:
                    for (int c = 0; c < CASCADES_PER_LIGHT; ++c)
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
                    break;
                }
            }

            TextureSpecifications textureSpecs;
            textureSpecs.borderColor = COL_RGBA(1.0f);
            textureSpecs.compareMode = TextureCompareMode::None;
            textureSpecs.minFilter = TextureFilter::Linear;
            textureSpecs.magFilter = TextureFilter::Linear;
            textureSpecs.wrapS = TextureWrap::ClampBorder;
            textureSpecs.wrapT = TextureWrap::ClampBorder;
            textureSpecs.wrapR = TextureWrap::ClampBorder;
            textureSpecs.internalFormat = TextureInternalFormat::Depth32F;
            textureSpecs.format = TextureFormat::Depth;

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

                    TextureSpecifications dirTextureSpecs = textureSpecs;
                    dirTextureSpecs.compareMode = TextureCompareMode::CompareRefToTexture;
                    dirTextureSpecs.compareFunc = TextureCompareFunc::LessOrEqual;

                    FramebufferSpecifications specs{};
                    specs.width  = m_DirShadowsResolution;
                    specs.height = m_DirShadowsResolution;
                    specs.hasDepth = true;
                    specs.hasColor = false;
                    specs.depthSpecs = dirTextureSpecs;

                    for (int c = 0; c < CASCADES_PER_LIGHT; ++c) {
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
                    specs.hasDepth = true;
                    specs.hasColor = false;
                    specs.depthSpecs = textureSpecs;

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
                    sm.lightPtr->shadowIndex = dirIndex;
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
    }

    void ShadowManager::TryShrinkCubeArray()
    {
        if (m_PointLightCount == 0) {
            m_CurrentPointLightCapacity = 0;
            if (m_CubeArrayTex && m_CubeArrayTex->IsValid()) {

                for (auto& sm : m_ShadowMaps)
                {
                    if (sm.second.light.type == (int)LightType::Point)
                    {
                        if (sm.second.framebuffer[0])
                        {
                            sm.second.framebuffer[0]->AttachCubemapArray(0);
                        }
                    }
                }

                m_CubeArrayTex.reset();
            }
            return;
        }

        // Only shrink if capacity is significantly larger than usage
        if (m_PointLightCount >= m_CurrentPointLightCapacity / 2 || m_CurrentPointLightCapacity <= 4)
            return;

        // Delete old array
        if(m_CubeArrayTex){
            if(m_CubeArrayTex->IsValid())
                m_CubeArrayTex.reset();
        }

        m_CurrentPointLightCapacity = std::max(4, m_PointLightCount); // never shrink below 4 layers

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

        std::vector<std::array<unsigned char *, 6>> emptyData;
        for(int i = 0; i < m_CurrentPointLightCapacity; i++){
            emptyData.push_back({nullptr, nullptr, nullptr, nullptr, nullptr, nullptr});
        }
        m_CubeArrayTex = CubemapArray::Create(texSpecs, emptyData);
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
            for (int c = 0; c < CASCADES_PER_LIGHT; ++c) {
                if (removedSM.framebuffer[c]->IsValid()){
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

            TryShrinkCubeArray();

            if (removedSM.framebuffer[0] && removedSM.framebuffer[0]->IsValid()) {
                removedSM.framebuffer[0]->Destroy();
                removedSM.framebuffer[0].reset();
            }
        }

        for(std::string passName : removedSM.passes){
            Core::GetEngine().GetRenderer()->RemoveRenderPass(passName);
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
                    int base = shadowIndex * CASCADES_PER_LIGHT;

                    for (int c = 0; c < CASCADES_PER_LIGHT; ++c)
                    {
                        material->SetTextureParameter(
                            "dirShadowMaps[" + std::to_string(base + c) + "]",
                            sm.second.framebuffer[c]->GetDepthAttachment()
                        );

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
                // Set sampler uniform
                material->SetTextureParameter(
                    "spotShadowMaps[" + std::to_string(shadowIndex) + "]",
                    sm.second.framebuffer[0]->GetDepthAttachment()
                );

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

        // Bind point light cube map array
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
                Core::GetEngine().GetRenderer()->RemoveRenderPass(passName);

            switch (sm.light.type)
            {
                case (int)LightType::Directional:
                    for (int c = 0; c < CASCADES_PER_LIGHT; ++c)
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

        m_PointLightCount = 0;
        m_CurrentPointLightCapacity = 0;
        m_CubeArrayTex.reset();
    }
}
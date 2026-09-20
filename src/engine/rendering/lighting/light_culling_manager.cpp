#include "light_culling_manager.hpp"

#include "engine/core/engine.hpp"

#include "engine/rendering/renderer/renderer.hpp"
#include "engine/rendering/renderer/renderer_api.hpp"
#include "engine/rendering/buffer/storage_buffer.hpp"
#include "engine/rendering/shader/compute_shader.hpp"
#include "engine/rendering/pipeline/compute_pipeline.hpp"
#include "engine/rendering/framebuffer/framebuffer.hpp"
#include "engine/rendering/lighting/light_manager.hpp"

#include "engine/objects/components/rendering/camera.hpp"
#include "engine/rendering/camera/camera_manager.hpp"

#include "engine/debugging/logger.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>

namespace Shard::Engine::Rendering {

    void LightCullingManager::Init(Renderer* renderer)
    {
        Filesystem::Path resRoot = Core::GetEngine().GetFileManager()->GetEngineResRoot();

        m_BuildShader = ComputeShader::Create(resRoot / "shaders/compute/cluster_build.comp");
        if (!m_BuildShader)
        {
            DEBUG_ERROR("LightCullingManager : failed to load cluster_build.comp");
            return;
        }

        ComputePipelineSpecifications buildSpecs;
        buildSpecs.shader = m_BuildShader;
        buildSpecs.debugName = "ClusterBuild";
        m_BuildPipeline = renderer->GetOrAddComputePipeline(buildSpecs);

        m_CullShader = ComputeShader::Create(resRoot / "shaders/compute/cluster_light_cull.comp");
        if (!m_CullShader)
        {
            DEBUG_ERROR("LightCullingManager : failed to load cluster_light_cull.comp");
            return;
        }

        ComputePipelineSpecifications cullSpecs;
        cullSpecs.shader = m_CullShader;
        cullSpecs.debugName = "ClusterLightCull";
        m_CullPipeline = renderer->GetOrAddComputePipeline(cullSpecs);

        m_ClusterAABBBuffer = StorageBuffer::Create(0);
        m_LightGridBuffer = StorageBuffer::Create(0);
        m_LightIndexListBuffer = StorageBuffer::Create(0);

        m_GlobalIndexCounterBuffer = StorageBuffer::Create(sizeof(uint32_t));
        uint32_t zero = 0;
        m_GlobalIndexCounterBuffer->SetData(&zero, sizeof(uint32_t));
    }

    void LightCullingManager::BindShadingBuffers()
    {
        m_LightGridBuffer->Bind(2);
        m_LightIndexListBuffer->Bind(3);
    }

    void LightCullingManager::Update()
    {
        if (!m_BuildPipeline || !m_CullPipeline)
            return;

        Renderer* renderer = Core::GetEngine().GetRenderer();

        auto cam = Core::GetEngine().GetCameraManager()->GetActiveCamera();
        if (!cam)
            return;

        auto viewport = renderer->GetViewportFramebuffer();
        if (!viewport)
            return;

        uint32_t width = viewport->GetWidth();
        uint32_t height = viewport->GetHeight();
        if (width == 0 || height == 0)
            return;

        m_GridSizeX = (width + kTilePx - 1) / kTilePx;
        m_GridSizeY = (height + kTilePx - 1) / kTilePx;
        uint32_t totalClusters = m_GridSizeX * m_GridSizeY * kZSlices;

        // Only (re)allocate the SSBOs when the cluster count actually changes (viewport resize) -
        // StorageBuffer::SetData does a plain glBufferSubData when the size is unchanged, which requires
        // a real data pointer, so this must not be called every frame with nullptr once the size settles.
        if (totalClusters != m_TotalClusters)
        {
            m_TotalClusters = totalClusters;
            m_ClusterAABBBuffer->SetData(nullptr, sizeof(glm::vec4) * 2 * m_TotalClusters);
            m_LightGridBuffer->SetData(nullptr, sizeof(glm::uvec2) * m_TotalClusters);
            m_LightIndexListBuffer->SetData(nullptr, sizeof(uint32_t) * m_TotalClusters * kMaxLightsPerCluster);
        }

        float nearPlane = std::max(cam->nearPlane, 0.001f);
        float farPlane = std::max(cam->farPlane, nearPlane + 0.001f);

        // Logarithmic Z-slice partition (Doom 2016 / Just Cause 3 clustered shading) - keeps slices
        // roughly equal-volume near vs far. Mirrored in lit.frag's GetClusterIndex to pick a fragment's
        // slice, and inverted in cluster_build.comp to get each slice's near/far Z.
        float logRatio = std::log2(farPlane / nearPlane);
        m_ClusterScaleZ = float(kZSlices) / logRatio;
        m_ClusterBiasZ = -(float(kZSlices) * std::log2(nearPlane) / logRatio);

        uint32_t groups = (m_TotalClusters + 63) / 64;

        // ---- Build cluster AABB grid ----
        m_BuildPipeline->Bind();
        m_ClusterAABBBuffer->Bind(1);

        m_BuildShader->SetMat4("invProjection", glm::inverse(cam->GetProjection()));
        m_BuildShader->SetInt("gridSizeX", (int)m_GridSizeX);
        m_BuildShader->SetInt("gridSizeY", (int)m_GridSizeY);
        m_BuildShader->SetFloat("nearPlane", nearPlane);
        m_BuildShader->SetFloat("farPlane", farPlane);
        m_BuildShader->SetVec2("screenSize", glm::vec2((float)width, (float)height));

        renderer->DispatchCompute(m_BuildPipeline, groups, 1, 1, MemoryBarrierBit::ShaderStorage);

        // ---- Cull lights into clusters ----
        uint32_t zero = 0;
        m_GlobalIndexCounterBuffer->SetData(&zero, sizeof(uint32_t));

        m_CullPipeline->Bind();

        // Binding point 0 is global GL state shared with the forward pass's own LightBuffer bind - rebind
        // defensively right before this dispatch reads it (see ProbeManager's note on this same hazard).
        renderer->GetLightManager()->GetSSBO()->Bind(0);

        m_ClusterAABBBuffer->Bind(1);
        m_LightGridBuffer->Bind(2);
        m_LightIndexListBuffer->Bind(3);
        m_GlobalIndexCounterBuffer->Bind(5);

        m_CullShader->SetMat4("view", cam->GetView());
        m_CullShader->SetInt("lightCount", renderer->GetLightManager()->GetLightsCount());
        m_CullShader->SetInt("gridSizeX", (int)m_GridSizeX);
        m_CullShader->SetInt("gridSizeY", (int)m_GridSizeY);

        renderer->DispatchCompute(m_CullPipeline, groups, 1, 1, MemoryBarrierBit::ShaderStorage);
    }
}

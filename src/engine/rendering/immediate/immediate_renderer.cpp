#include "immediate_renderer.hpp"

#include "engine/core/engine.hpp"
#include "engine/core/resources/resources_manager.hpp"

#include "engine/rendering/renderer/renderer.hpp"
#include "engine/rendering/renderer/renderer_api.hpp"
#include "engine/rendering/buffer/storage_buffer.hpp"
#include "engine/rendering/lighting/light_manager.hpp"
#include "engine/rendering/lighting/light_culling_manager.hpp"
#include "engine/rendering/material/material.hpp"
#include "engine/rendering/pipeline/pipeline.hpp"
#include "engine/rendering/texture/cubemap/envmap.hpp"
#include "engine/rendering/texture/image_export.hpp"
#include "engine/rendering/shader/shader.hpp"

#include "engine/debugging/logger.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Shard::Engine::Rendering {

    namespace {
        // Mirrors common/cluster.glsl's CLUSTER_Z_SLICES : a 1x1 grid has one cluster per Z slice, and
        // the studio view always lands in slice 0 (clusterScaleZ = clusterBiasZ = 0), but size the
        // buffer for all of them so a shader change can't read past its end.
        constexpr uint32_t kClusterZSlices = 24;
        constexpr uint32_t kMaxTargetSize = 4096;
    }

    ImmediateTarget& ImmediateTarget::operator=(ImmediateTarget&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            m_Owner = other.m_Owner;
            m_Framebuffer = std::move(other.m_Framebuffer);
            other.m_Owner = nullptr;
            other.m_Framebuffer = nullptr;
        }
        return *this;
    }

    void ImmediateTarget::Release()
    {
        if (m_Owner && m_Framebuffer)
            m_Owner->ReleaseTarget(m_Framebuffer);
        m_Owner = nullptr;
        m_Framebuffer = nullptr;
    }

    void ImmediateRenderer::Init(Renderer* renderer)
    {
        m_Renderer = renderer;
    }

    void ImmediateRenderer::Shutdown()
    {
        for (auto& entry : m_Pool)
            entry.framebuffer->Destroy();
        m_Pool.clear();
    }

    std::shared_ptr<Framebuffer> ImmediateRenderer::AcquireTarget(uint32_t width, uint32_t height, bool multisample)
    {
        for (auto& entry : m_Pool)
        {
            const auto& specs = entry.framebuffer->GetSpecifications();
            if (!entry.inUse && specs.width == width && specs.height == height && specs.multisampled == multisample)
            {
                entry.inUse = true;
                return entry.framebuffer;
            }
        }

        // Keep the pool bounded : make room by dropping idle targets of other sizes (oldest first).
        while (m_Pool.size() >= kMaxPooledTargets)
        {
            auto idle = std::find_if(m_Pool.begin(), m_Pool.end(), [](const PoolEntry& e) { return !e.inUse; });
            if (idle == m_Pool.end())
                return nullptr;
            idle->framebuffer->Destroy();
            m_Pool.erase(idle);
        }

        FramebufferSpecifications specs;
        specs.width = width;
        specs.height = height;
        specs.multisampled = multisample;
        specs.samplesCount = 4;
        specs.hasColor = true;
        specs.hasDepth = true;

        auto framebuffer = Framebuffer::Create(specs);
        if (!framebuffer || !framebuffer->IsValid())
            return nullptr;

        m_Pool.push_back({framebuffer, true});
        return framebuffer;
    }

    void ImmediateRenderer::ReleaseTarget(const std::shared_ptr<Framebuffer>& framebuffer)
    {
        for (auto& entry : m_Pool)
        {
            if (entry.framebuffer == framebuffer)
            {
                entry.inUse = false;
                return;
            }
        }
    }

    void ImmediateRenderer::EnsureStudioRig()
    {
        if (m_StudioLights)
            return;

        // Lights are given by where they sit relative to the subject (the direction TOWARD the light),
        // tuned for FrameBounds()'s fixed 3/4 view : a warm key from above-front-right, a cool fill from
        // the left, and a rim from behind to separate the silhouette from the background.
        auto makeLight = [](const glm::vec3& toLight, const glm::vec3& color, float intensity)
        {
            LightData light;
            light.direction = glm::vec4(-glm::normalize(toLight), 0.0f);
            light.color = glm::vec4(color, 1.0f);
            light.intensity = intensity;
            light.type = LightType::Directional;
            light.castShadow = 0;
            light.shadowIndex = -1;
            return light;
        };

        LightData lights[kStudioLightCount] = {
            makeLight({ 0.6f, 1.0f,  1.0f}, {1.00f, 0.98f, 0.95f}, 3.0f),
            makeLight({-1.0f, 0.2f,  0.8f}, {0.85f, 0.90f, 1.00f}, 0.9f),
            makeLight({-0.4f, 0.5f, -1.0f}, {1.00f, 1.00f, 1.00f}, 1.4f),
        };

        m_StudioLights = StorageBuffer::Create(sizeof(lights));
        m_StudioLights->SetData(lights, sizeof(lights));

        // One uvec2 (offset, count) per cluster, all zero = no lights anywhere.
        std::vector<uint32_t> emptyGrid(kClusterZSlices * 2, 0);
        m_EmptyClusterGrid = StorageBuffer::Create(uint32_t(emptyGrid.size() * sizeof(uint32_t)));
        m_EmptyClusterGrid->SetData(emptyGrid.data(), uint32_t(emptyGrid.size() * sizeof(uint32_t)));

        uint32_t emptyIndices[4] = {};
        m_EmptyClusterIndices = StorageBuffer::Create(sizeof(emptyIndices));
        m_EmptyClusterIndices->SetData(emptyIndices, sizeof(emptyIndices));
    }

    void ImmediateRenderer::EnsureNeutralEnvironment()
    {
        if (m_NeutralEnvironment)
            return;

        // EnvironmentMap is only buildable from image files, so write six tiny flat faces to a temp folder
        // (+X, -X, +Y, -Y, +Z, -Z) : brighter sky, darker ground, mid-grey sides.
        const uint8_t faceGrey[6] = {150, 150, 210, 70, 150, 150};
        constexpr uint32_t kFaceSize = 4;

        std::filesystem::path dir = std::filesystem::temp_directory_path() / "shard_neutral_env";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        std::vector<Filesystem::Path> faces;
        for (int i = 0; i < 6; ++i)
        {
            std::vector<uint8_t> pixels(kFaceSize * kFaceSize * 3, faceGrey[i]);
            Filesystem::Path path((dir / ("face" + std::to_string(i) + ".png")).string(), true);
            if (!ImageExport::WritePNG(path, kFaceSize, kFaceSize, 3, pixels.data()))
            {
                DEBUG_ERROR("ImmediateRenderer : could not write neutral environment face ", path.full);
                return;
            }
            faces.push_back(path);
        }

        TextureSpecifications specs = {};
        m_NeutralEnvironment = EnvironmentMap::Create(specs, faces);
        if (!m_NeutralEnvironment)
            DEBUG_ERROR("ImmediateRenderer : failed to build the neutral environment map");

        // Building it renders into framebuffers behind the API's back.
        m_Renderer->GetRendererAPI()->InvalidateStateCache();

        std::filesystem::remove_all(dir, ec);
    }

    void ImmediateRenderer::EnsureUnlitMaterial()
    {
        if (m_UnlitMaterial)
            return;

        PipelineSpecifications specs;
        specs.shader = Core::GetEngine().GetResourcesManager()->GetShader("shaders/mesh/unlit");
        specs.depthTest = true;
        specs.depthWrite = true;
        specs.blending = false;
        specs.cullMode = CullMode::Back;
        specs.debugName = "ImmediateUnlit";

        m_UnlitPipeline = m_Renderer->GetOrAddPipeline(specs);
        m_UnlitMaterial = Material::Create(specs.shader, m_UnlitPipeline, false, Opacity::Opaque);
    }

    ImmediateTarget ImmediateRenderer::Render(const ImmediateDesc& desc, const std::vector<DrawCommand>& commands)
    {
        ImmediateTarget result;

        if (!m_Renderer)
        {
            DEBUG_ERROR("ImmediateRenderer::Render called before Init");
            return result;
        }
        if (m_Rendering)
        {
            DEBUG_ERROR("ImmediateRenderer::Render is not re-entrant");
            return result;
        }
        if (desc.width == 0 || desc.height == 0 || desc.width > kMaxTargetSize || desc.height > kMaxTargetSize)
        {
            DEBUG_ERROR("ImmediateRenderer::Render : invalid target size ", desc.width, "x", desc.height);
            return result;
        }

        auto framebuffer = AcquireTarget(desc.width, desc.height, desc.multisample);
        if (!framebuffer)
        {
            DEBUG_ERROR("ImmediateRenderer::Render : no render target available");
            return result;
        }

        m_Rendering = true;

        auto pass = std::make_shared<RenderPass>();
        pass->target = framebuffer;
        pass->clearColor = true;
        pass->clearDepth = true;
        pass->allowResize = false;
        pass->allowCulling = false;
        pass->drawList = commands;

        // Both modes use the studio view : Unlit ignores the lights, but still must not depend on
        // (or touch) the level's lighting state.
        EnsureStudioRig();
        if (desc.lighting == ImmediateLighting::Studio)
            EnsureNeutralEnvironment();

        RenderView view = desc.view;
        view.lighting = ViewLighting::Studio;
        view.studioLightCount = kStudioLightCount;
        view.studioAmbient = kStudioAmbient;
        view.studioEnvironment = m_NeutralEnvironment;

        if (desc.lighting == ImmediateLighting::Unlit)
        {
            EnsureUnlitMaterial();

            pass->overridePipeline = true;
            pass->customPipeline = m_UnlitPipeline;
            pass->customUniforms["useTexture"] = false;
            pass->customUniforms["useCustomColor"] = true;
            pass->customUniforms["customColor"] = desc.unlitColor;
            pass->customUniforms["masked"] = false;

            // The pass only skips commands with no material - the pipeline override supplies the shader.
            for (auto& command : pass->drawList)
                if (!command.material)
                    command.material = m_UnlitMaterial;
        }

        m_StudioLights->Bind(0);
        m_EmptyClusterGrid->Bind(2);
        m_EmptyClusterIndices->Bind(3);

        RendererAPI* api = m_Renderer->GetRendererAPI();
        const glm::vec4 previousClearColor = api->GetClearColor();
        api->SetClearColor(desc.clearColor.r, desc.clearColor.g, desc.clearColor.b, desc.clearColor.a);

        m_Renderer->RenderImmediate(pass, view);

        // The main frame's clear color is whatever the app last set - hand it back untouched.
        api->SetClearColor(previousClearColor.r, previousClearColor.g, previousClearColor.b, previousClearColor.a);

        framebuffer->ResolveMultisampled();
        framebuffer->Unbind();

        // Slots 0/2/3 are global state : hand them back to the scene's buffers.
        m_Renderer->GetLightManager()->GetSSBO()->Bind(0);
        if (auto culling = m_Renderer->GetLightCullingManager())
            culling->BindShadingBuffers();

        api->InvalidateStateCache();

        m_Rendering = false;

        result.m_Owner = this;
        result.m_Framebuffer = framebuffer;
        return result;
    }

    bool ImmediateRenderer::ReadPixels(const ImmediateTarget& target, std::vector<uint8_t>& outPixels)
    {
        if (!target.IsValid())
            return false;

        bool ok = target.GetFramebuffer()->ReadPixelsRGBA8(outPixels);
        m_Renderer->GetRendererAPI()->InvalidateStateCache();
        return ok;
    }

    void ImmediateRenderer::CopyTo(const ImmediateTarget& target, Framebuffer& dst, uint32_t dstX, uint32_t dstY, uint32_t dstWidth, uint32_t dstHeight)
    {
        if (!target.IsValid())
            return;

        target.GetFramebuffer()->BlitColorTo(dst, dstX, dstY, dstWidth, dstHeight);
        m_Renderer->GetRendererAPI()->InvalidateStateCache();
    }

    RenderView ImmediateRenderer::FrameBounds(const glm::vec3& boundsMin, const glm::vec3& boundsMax, float aspect, float fovDegrees)
    {
        glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
        float radius = glm::length((boundsMax - boundsMin) * 0.5f);
        if (radius < 0.001f)
            radius = 1.0f;

        // Fit the bounding sphere in the narrower of the two fields of view, with 30% breathing room.
        float halfFov = glm::radians(fovDegrees * 0.5f);
        if (aspect < 1.0f)
            halfFov = atanf(tanf(halfFov) * aspect);
        float distance = (radius / sinf(halfFov)) * 1.f;

        glm::vec3 dir = glm::normalize(glm::vec3(1.0f, 0.8f, 1.2f));
        glm::vec3 eye = center + dir * distance;

        RenderView view;
        view.view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
        view.projection = glm::perspective(glm::radians(fovDegrees), aspect, distance * 0.01f, distance + radius * 2.0f + 1.0f);
        view.position = eye;
        view.orthographic = false;
        return view;
    }
}

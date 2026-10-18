#include "ssr_manager.hpp"

#include "engine/world/engine.hpp"
#include "engine/core/diagnostics/logger.hpp"

#include "engine/renderer/features/lighting/ssao_manager.hpp"
#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/renderer/rhi/gpu_profiler.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/renderer/rhi/shader/compute_shader.hpp"
#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"
#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>

namespace Shard::Engine::Rendering {

    namespace {

        // Texture units the chain's samplers sit on : above the 0-31 the forward shader uses, clear of the occlusion culling's (47, 48) and the probes' (44+)
        constexpr uint32_t kUnitSource = 49;      // the depth of the prepass / the last picture (level 0 copies)
        constexpr uint32_t kUnitHiZ = 50;
        constexpr uint32_t kUnitNormal = 51;
        constexpr uint32_t kUnitHistory = 52;     // the previous trace / the trace, for the resolve

        constexpr uint32_t kMaxHiZLevels = 10;        // a cell of the top level is 512 pixels wide : more would be steps the march never takes
        constexpr uint32_t kMaxPyramidLevels = 9;     // a blur of more than 256 pixels does not look like a reflection any more

        uint32_t MipLevelsOf(uint32_t width, uint32_t height)
        {
            return static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(std::max(width, height))))) + 1;
        }

        std::shared_ptr<ComputeShader> LoadShader(const Filesystem::Path& root, const char* name)
        {
            std::shared_ptr<ComputeShader> shader = ComputeShader::Create(root / "shaders/compute/ssr" / name);
            if (!shader)
                DEBUG_ERROR(std::string("SSRManager : failed to load ") + name);
            return shader;
        }

        std::shared_ptr<ComputePipeline> MakePipeline(IRenderContext* renderer, const std::shared_ptr<ComputeShader>& shader, const char* debugName)
        {
            if (!shader)
                return nullptr;

            ComputePipelineSpecifications specs;
            specs.shader = shader;
            specs.debugName = debugName;
            return renderer->GetOrAddComputePipeline(specs);
        }

        std::shared_ptr<Texture2D> MakeTexture(uint32_t width, uint32_t height, TextureFormat format, TextureInternalFormat internalFormat,
                                               bool mips, TextureFilter minFilter, TextureFilter magFilter)
        {
            TextureSpecifications specs;
            specs.width = width;
            specs.height = height;
            specs.format = format;
            specs.internalFormat = internalFormat;
            specs.minFilter = minFilter;
            specs.magFilter = magFilter;
            specs.wrapS = TextureWrap::ClampEdge;
            specs.wrapT = TextureWrap::ClampEdge;
            specs.generateMips = mips;       // the whole chain is allocated
            specs.immutableStorage = true;   // a level can only be bound as an image of an immutable texture

            return Texture2D::Create(specs, static_cast<const void*>(nullptr));
        }
    }

    void SSRManager::Init(IRenderContext* renderer)
    {
        m_Renderer = renderer;

        // The chain is image load / store and bindless handles, which the OpenGL and Vulkan backends both have. Headless runs draw nothing.
        if (RendererAPI::Current() && RendererAPI::Current()->GetAPI() == RendererAPI::API::Null)
            return;

        const Filesystem::Path root = Core::GetEngine().GetFileManager()->GetEngineResRoot();

        m_HiZInitShader = LoadShader(root, "ssr_hiz_init.comp");
        m_HiZReduceShader = LoadShader(root, "ssr_hiz_reduce.comp");
        m_ColorInitShader = LoadShader(root, "ssr_color_init.comp");
        m_ColorReduceShader = LoadShader(root, "ssr_color_reduce.comp");
        m_TraceShader = LoadShader(root, "ssr_trace.comp");
        m_ResolveShader = LoadShader(root, "ssr_resolve.comp");

        m_HiZInitPipeline = MakePipeline(renderer, m_HiZInitShader, "SSRHiZInit");
        m_HiZReducePipeline = MakePipeline(renderer, m_HiZReduceShader, "SSRHiZReduce");
        m_ColorInitPipeline = MakePipeline(renderer, m_ColorInitShader, "SSRColorInit");
        m_ColorReducePipeline = MakePipeline(renderer, m_ColorReduceShader, "SSRColorReduce");
        m_TracePipeline = MakePipeline(renderer, m_TraceShader, "SSRTrace");
        m_ResolvePipeline = MakePipeline(renderer, m_ResolveShader, "SSRResolve");

        m_Ready = m_HiZInitPipeline && m_HiZReducePipeline && m_ColorInitPipeline && m_ColorReducePipeline && m_TracePipeline && m_ResolvePipeline;
        if (!m_Ready)
        {
            DEBUG_ERROR("SSRManager : a shader is missing, the screen-space reflections stay off.");
            return;
        }

        // The pass stands for the chain in the render graph : it comes after the prepass it reads, before whatever lights the scene. It draws
        // nothing (no target), and must not be dropped for it.
        std::shared_ptr<RenderPass> pass = std::make_shared<RenderPass>();
        pass->target = nullptr;
        pass->allowResize = false;
        pass->allowCulling = false;
        pass->graphSideEffects = true;
        pass->enabled = false;
        renderer->AddRenderPass(pass, kPassName, {"SSAODepthNormalPass"});
        renderer->AddDependencyToPass("ForwardPass", kPassName);
    }

    void SSRManager::Update()
    {
        m_Produced = false;

        if (!m_Ready)
            return;

        const RenderSnapshot& snapshot = m_Renderer->GetSnapshot();
        const SSRSettings& settings = snapshot.environment.ssr;
        const CameraProxy& camera = snapshot.camera;

        // An orthographic view has no perspective for the march's straight line in screen space to rely on : the pass stays off
        const bool active = settings.enabled && camera.valid && !camera.orthographic;

        if (auto pass = m_Renderer->GetRenderPass(kPassName))
            pass->enabled = active;

        if (!active)
            m_HistoryValid = false;

        if (!camera.valid)
            return;

        // The frame before's camera is where the picture the reflections are read from was seen from
        m_PrevViewProj = m_HaveCamera ? m_ViewProj : camera.viewProjection;
        m_ViewProj = camera.viewProjection;

        // How much the view moved : the reflection is view dependent, so a history blended while it moves would trail
        if (m_HaveCamera)
        {
            const float moved = glm::length(camera.position - m_PrevPosition);
            const float turned = 1.0f - glm::clamp(glm::dot(camera.forward, m_PrevForward), 0.0f, 1.0f);
            m_CameraStill = std::exp(-(moved * 20.0f + turned * 4000.0f));
        }
        else
        {
            m_CameraStill = 0.0f;
        }

        m_PrevPosition = camera.position;
        m_PrevForward = camera.forward;
        m_HaveCamera = true;
    }

    bool SSRManager::EnsureResources(uint32_t width, uint32_t height, int divisor)
    {
        if (m_Result && m_Width == width && m_Height == height && m_Divisor == divisor)
            return true;

        m_Width = width;
        m_Height = height;
        m_Divisor = divisor;
        m_TraceWidth = std::max(1u, (width + divisor - 1) / divisor);
        m_TraceHeight = std::max(1u, (height + divisor - 1) / divisor);

        m_HiZLevels = std::min(MipLevelsOf(width, height), kMaxHiZLevels);
        m_PyramidLevels = std::min(MipLevelsOf(width, height), kMaxPyramidLevels);

        m_HiZ = MakeTexture(width, height, TextureFormat::RED, TextureInternalFormat::R32F, true, TextureFilter::NearestMipmapNearest, TextureFilter::Nearest);
        m_Pyramid = MakeTexture(width, height, TextureFormat::RGBA, TextureInternalFormat::RGBA16F, true, TextureFilter::LinearMipmapLinear, TextureFilter::Linear);
        m_Result = MakeTexture(width, height, TextureFormat::RGBA, TextureInternalFormat::RGBA16F, false, TextureFilter::Nearest, TextureFilter::Nearest);
        for (auto& history : m_History)
            history = MakeTexture(m_TraceWidth, m_TraceHeight, TextureFormat::RGBA, TextureInternalFormat::RGBA32F, false, TextureFilter::Nearest, TextureFilter::Nearest);

        m_HistoryValid = false;
        m_HistoryIndex = 0;

        if (!m_HiZ || !m_Pyramid || !m_Result || !m_History[0] || !m_History[1])
        {
            m_Result.reset();
            DEBUG_ERROR("SSRManager : the textures could not be created.");
            return false;
        }

        // Read by lit.frag through bindless handles (a texture unit is not to be had there, see SampleSSR)
        m_ResultHandle = m_Result->GetBindlessHandle();
        m_PyramidHandle = m_Pyramid->GetBindlessHandle();
        return true;
    }

    void SSRManager::Dispatch(const std::shared_ptr<ComputePipeline>& pipeline, uint32_t width, uint32_t height)
    {
        m_Renderer->DispatchCompute(pipeline, (width + 7) / 8, (height + 7) / 8, 1, MemoryBarrierBit::ImageAccess | MemoryBarrierBit::TextureFetch);
    }

    void SSRManager::Execute(uint32_t sceneColor)
    {
        m_Produced = false;

        if (!m_Ready || sceneColor == 0)
            return;

        const RenderSnapshot& snapshot = m_Renderer->GetSnapshot();
        const SSRSettings& settings = snapshot.environment.ssr;
        const CameraProxy& camera = snapshot.camera;
        if (!settings.enabled || !camera.valid || camera.orthographic)
            return;

        const std::shared_ptr<SSAOManager> ssao = m_Renderer->GetSSAOManager();
        const std::shared_ptr<Framebuffer> prepass = ssao ? ssao->GetPrepassFramebuffer() : nullptr;
        if (!prepass || prepass->GetWidth() == 0 || prepass->GetHeight() == 0)
            return;

        const uint32_t width = prepass->GetWidth();
        const uint32_t height = prepass->GetHeight();
        if (!EnsureResources(width, height, settings.ClampedDivisor()))
            return;

        RendererAPI* api = m_Renderer->GetRendererAPI();
        GpuProfiler* profiler = api->GetGpuProfiler();

        const int hizTop = static_cast<int>(m_HiZLevels) - 1;

        // ---- 1. Hi-Z : the closest depth, level by level ----
        {
            GpuScope scope(profiler, "SSR Hi-Z");

            m_HiZInitPipeline->Bind();
            api->BindTextureUnit(kUnitSource, prepass->GetDepthAttachment());
            m_HiZ->BindImage(0, TextureAccess::WriteOnly, 0);
            m_HiZInitShader->SetInt("uSizeX", static_cast<int>(width));
            m_HiZInitShader->SetInt("uSizeY", static_cast<int>(height));
            Dispatch(m_HiZInitPipeline, width, height);

            uint32_t srcWidth = width, srcHeight = height;
            for (uint32_t level = 1; level < m_HiZLevels; level++)
            {
                const uint32_t dstWidth = std::max(srcWidth / 2, 1u);
                const uint32_t dstHeight = std::max(srcHeight / 2, 1u);

                m_HiZReducePipeline->Bind();
                m_HiZ->BindImage(0, TextureAccess::ReadOnly, level - 1);
                m_HiZ->BindImage(1, TextureAccess::WriteOnly, level);
                m_HiZReduceShader->SetInt("uSrcSizeX", static_cast<int>(srcWidth));
                m_HiZReduceShader->SetInt("uSrcSizeY", static_cast<int>(srcHeight));
                m_HiZReduceShader->SetInt("uDstSizeX", static_cast<int>(dstWidth));
                m_HiZReduceShader->SetInt("uDstSizeY", static_cast<int>(dstHeight));
                Dispatch(m_HiZReducePipeline, dstWidth, dstHeight);

                srcWidth = dstWidth;
                srcHeight = dstHeight;
            }
        }

        // ---- 2. The colour the reflections show : the last picture, with the mips that blur it ----
        {
            GpuScope scope(profiler, "SSR Colour");

            m_ColorInitPipeline->Bind();
            api->BindTextureUnit(kUnitSource, sceneColor);
            m_Pyramid->BindImage(0, TextureAccess::WriteOnly, 0);
            m_ColorInitShader->SetInt("uSizeX", static_cast<int>(width));
            m_ColorInitShader->SetInt("uSizeY", static_cast<int>(height));
            Dispatch(m_ColorInitPipeline, width, height);

            uint32_t srcWidth = width, srcHeight = height;
            for (uint32_t level = 1; level < m_PyramidLevels; level++)
            {
                const uint32_t dstWidth = std::max(srcWidth / 2, 1u);
                const uint32_t dstHeight = std::max(srcHeight / 2, 1u);

                m_ColorReducePipeline->Bind();
                m_Pyramid->BindImage(0, TextureAccess::ReadOnly, level - 1);
                m_Pyramid->BindImage(1, TextureAccess::WriteOnly, level);
                m_ColorReduceShader->SetInt("uSrcSizeX", static_cast<int>(srcWidth));
                m_ColorReduceShader->SetInt("uSrcSizeY", static_cast<int>(srcHeight));
                m_ColorReduceShader->SetInt("uDstSizeX", static_cast<int>(dstWidth));
                m_ColorReduceShader->SetInt("uDstSizeY", static_cast<int>(dstHeight));
                Dispatch(m_ColorReducePipeline, dstWidth, dstHeight);

                srcWidth = dstWidth;
                srcHeight = dstHeight;
            }
        }

        const glm::mat4 projection = camera.projection;
        const glm::mat4 invProjection = glm::inverse(projection);
        const glm::mat4 invView = glm::inverse(camera.view);
        const bool temporal = settings.temporal && m_HistoryValid;
        const int writeIndex = m_HistoryIndex;
        const int readIndex = 1 - m_HistoryIndex;

        // ---- 3. The march ----
        {
            GpuScope scope(profiler, "SSR Trace");

            m_TracePipeline->Bind();
            api->BindTextureUnit(kUnitHiZ, m_HiZ->GetHandle());
            api->BindTextureUnit(kUnitNormal, prepass->GetColorAttachment());
            api->BindTextureUnit(kUnitHistory, m_History[readIndex]->GetHandle());
            m_History[writeIndex]->BindImage(0, TextureAccess::WriteOnly, 0);

            m_TraceShader->SetMat4("uProjection", projection);
            m_TraceShader->SetMat4("uInvProjection", invProjection);
            m_TraceShader->SetMat4("uInvView", invView);
            m_TraceShader->SetMat4("uPrevViewProj", m_PrevViewProj);
            m_TraceShader->SetInt("uFullSizeX", static_cast<int>(width));
            m_TraceShader->SetInt("uFullSizeY", static_cast<int>(height));
            m_TraceShader->SetInt("uTraceSizeX", static_cast<int>(m_TraceWidth));
            m_TraceShader->SetInt("uTraceSizeY", static_cast<int>(m_TraceHeight));
            m_TraceShader->SetInt("uDivisor", m_Divisor);
            m_TraceShader->SetInt("uMaxSteps", std::clamp(settings.maxSteps, 4, 256));
            m_TraceShader->SetInt("uMaxLevel", hizTop);
            m_TraceShader->SetFloat("uMaxDistance", std::max(settings.maxDistance, 0.1f));
            m_TraceShader->SetFloat("uThickness", std::max(settings.thickness, 0.001f));
            m_TraceShader->SetFloat("uNear", std::max(camera.nearPlane, 0.001f));
            m_TraceShader->SetFloat("uEdgeFade", std::clamp(settings.edgeFade, 0.0f, 0.5f));
            m_TraceShader->SetBool("uTemporal", temporal);
            m_TraceShader->SetFloat("uTemporalBlend", std::clamp(settings.temporalBlend, 0.0f, 0.98f));
            m_TraceShader->SetFloat("uCameraStill", m_CameraStill);
            Dispatch(m_TracePipeline, m_TraceWidth, m_TraceHeight);
        }

        // ---- 4. One entry per pixel of the screen ----
        {
            GpuScope scope(profiler, "SSR Resolve");

            m_ResolvePipeline->Bind();
            api->BindTextureUnit(kUnitHiZ, m_HiZ->GetHandle());
            api->BindTextureUnit(kUnitNormal, prepass->GetColorAttachment());
            api->BindTextureUnit(kUnitHistory, m_History[writeIndex]->GetHandle());
            m_Result->BindImage(0, TextureAccess::WriteOnly, 0);

            m_ResolveShader->SetMat4("uProjection", projection);
            m_ResolveShader->SetMat4("uInvProjection", invProjection);
            m_ResolveShader->SetMat4("uView", camera.view);
            m_ResolveShader->SetMat4("uPrevViewProj", m_PrevViewProj);
            m_ResolveShader->SetInt("uFullSizeX", static_cast<int>(width));
            m_ResolveShader->SetInt("uFullSizeY", static_cast<int>(height));
            m_ResolveShader->SetInt("uTraceSizeX", static_cast<int>(m_TraceWidth));
            m_ResolveShader->SetInt("uTraceSizeY", static_cast<int>(m_TraceHeight));
            m_ResolveShader->SetInt("uDivisor", m_Divisor);
            Dispatch(m_ResolvePipeline, width, height);
        }

        m_HistoryIndex = readIndex;
        m_HistoryValid = true;
        m_Produced = true;
    }

    void SSRManager::BindSSR(const std::shared_ptr<Material>& material, bool allowed)
    {
        if (!material)
            return;

        std::shared_ptr<Shader> shader = material->GetShader();
        if (!shader)
            return;

        const SSRSettings& settings = m_Renderer->GetSnapshot().environment.ssr;
        const bool active = allowed && m_Produced && settings.enabled && m_ResultHandle != 0 && m_PyramidHandle != 0;

        shader->SetBool("ssrEnabled", active);
        if (!active)
            return;

        shader->SetFloat("ssrIntensity", std::max(settings.intensity, 0.0f));
        shader->SetFloat("ssrMaxRoughness", std::clamp(settings.maxRoughness, 0.01f, 1.0f));
        shader->SetFloat("ssrRoughnessBlur", std::max(settings.roughnessBlur, 0.0f));
        shader->SetFloat("ssrMaxLod", static_cast<float>(m_PyramidLevels - 1));
        shader->SetUVec2("ssrHitHandle", static_cast<uint32_t>(m_ResultHandle & 0xFFFFFFFFu), static_cast<uint32_t>(m_ResultHandle >> 32));
        shader->SetUVec2("ssrColorHandle", static_cast<uint32_t>(m_PyramidHandle & 0xFFFFFFFFu), static_cast<uint32_t>(m_PyramidHandle >> 32));
    }

}

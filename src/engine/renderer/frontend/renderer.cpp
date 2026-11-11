#include "renderer.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/renderer/rhi/gpu_profiler.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/features/lighting/shadow_manager.hpp"
#include "engine/renderer/features/lighting/probe_manager.hpp"
#include "engine/renderer/features/lighting/lightmap_manager.hpp"
#include "engine/renderer/features/lighting/ssao_manager.hpp"
#include "engine/renderer/features/lighting/ssr_manager.hpp"
#include "engine/renderer/features/lighting/light_culling_manager.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/renderer/frontend/render_asset_kinds.hpp"
#include "engine/core/diagnostics/profiler.hpp"
#include "engine/renderer/rhi/frustum.hpp"
#include <queue>
#include <unordered_set>
#include "engine/renderer/features/culling/occlusion_culling_manager.hpp"
#include <algorithm>
#include <glm/gtx/string_cast.hpp>

#include "engine/core/config/cvar.hpp"
#include "engine/assets/project/project.hpp"
#include "engine/renderer/frontend/pipelines/render_pipeline.hpp"
#include "engine/renderer/rhi/resources/texture/image_export.hpp"
#include <filesystem>
#include <fstream>

namespace Shard::Engine::Rendering{

    namespace {

        Core::CVar<bool> cvDepthPrepass("render.depth_prepass", true, "Lay the depth of the opaque geometry down first so every pixel is shaded once (see render.gpu)");
        Core::CVar<bool> cvGpuTimers("render.gpu_timers", false, "Measure the GPU time of each render pass (see render.gpu)");

        // FNV-1a over raw bytes, only for change detection. A collision costs a stale frame.
        uint64_t HashBytes(uint64_t h, const void* data, size_t size)
        {
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; i++)
            {
                h ^= bytes[i];
                h *= 1099511628211ULL;
            }
            return h;
        }

        template<typename T>
        uint64_t HashValue(uint64_t h, const T& value)
        {
            return HashBytes(h, &value, sizeof(T));
        }

        uint64_t HashDrawList(const std::vector<DrawCommand>& list)
        {
            uint64_t h = 1469598103934665603ULL;
            h = HashValue(h, list.size());

            for (const DrawCommand& cmd : list)
            {
                h = HashValue(h, cmd.commandID);
                h = HashValue(h, cmd.indexOffset);
                h = HashValue(h, cmd.indexCount);
                h = HashValue(h, cmd.mesh.get());
                h = HashValue(h, cmd.material.get());
                h = HashValue(h, cmd.material && cmd.material->IsDoubleSided());
                h = HashValue(h, cmd.modelMatrix);
                h = HashValue(h, cmd.lightmapHandle);
                h = HashValue(h, cmd.lightmapScaleOffset);
            }

            return h;
        }

        uint64_t HashPassState(const RenderPass& pass, uint64_t drawListHash)
        {
            uint64_t h = drawListHash;

            h = HashValue(h, pass.target.get());
            if (pass.target)
                h = HashValue(h, pass.target->GetDepthAttachment());
            h = HashValue(h, pass.customPipeline.get());

            for (const auto& [name, value] : pass.customUniforms)
            {
                h = HashBytes(h, name.data(), name.size());
                h = HashValue(h, value.index());
                std::visit([&h](const auto& v) { h = HashValue(h, v); }, value);
            }

            return h;
        }


        constexpr const char* kViewportResource = "ViewportColor";
        constexpr const char* kSceneResource = "SceneColor";
        constexpr const char* kResolvePassName = "SceneResolvePass";

        uint32_t BytesPerTexel(TextureInternalFormat format)
        {
            switch (format)
            {
                case TextureInternalFormat::RED:        return 1;
                case TextureInternalFormat::R16F:
                case TextureInternalFormat::RG:
                case TextureInternalFormat::Depth16:    return 2;
                case TextureInternalFormat::RGB:
                case TextureInternalFormat::RGB8:       return 3;
                case TextureInternalFormat::RG16F:
                case TextureInternalFormat::RGBA8:
                case TextureInternalFormat::RGBA:
                case TextureInternalFormat::Depth24Stencil8:
                case TextureInternalFormat::Depth24:
                case TextureInternalFormat::Depth32:
                case TextureInternalFormat::Depth32F:
                case TextureInternalFormat::R32F:
                case TextureInternalFormat::RGB9E5:     return 4;
                case TextureInternalFormat::RGB16F:     return 6;
                case TextureInternalFormat::RGBA16F:    return 8;
                case TextureInternalFormat::RGB32I:
                case TextureInternalFormat::RGB32F:     return 12;
                case TextureInternalFormat::RGBA32I:
                case TextureInternalFormat::RGBA32F:    return 16;
            }
            return 4;
        }

        uint32_t FramebufferBytesPerTexel(const FramebufferSpecifications& specs)
        {
            const uint32_t copies = specs.multisampled ? specs.samplesCount + 1 : 1;
            uint32_t bytes = 0;
            if (specs.hasColor)
                bytes += BytesPerTexel(specs.colorSpecs.internalFormat) * copies;
            if (specs.hasDepth)
                bytes += BytesPerTexel(specs.depthSpecs.internalFormat) * copies;
            return bytes;
        }
    }








    void Renderer::Init(std::shared_ptr<RendererSettings> initialSettings)
    {
        m_Settings = initialSettings;

        RegisterAssetKinds(*Core::GetEngine().GetResourcesManager());

        m_RendererAPI = RendererAPI::Create(initialSettings->api);
        if (!m_RendererAPI)
        {
            DEBUG_FATAL("The graphics backend could not be created (see the messages above).");
            return;
        }
        RendererAPI::SetCurrent(m_RendererAPI.get());
        m_RendererAPI->SetSceneBinding(this);

        if (!m_RendererAPI->SupportsBlockCompression())
        {
            DEBUG_WARNING("The GPU does not support BC4 / BC5 / BC7 textures : texture compression is off.");
            Core::CVarRegistry::Global().Execute("texture.compress false");
        }

        if (Debugging::Profiler* profiler = Debugging::Profiler::Active())
        {
            profiler->AddStatsProvider([this](Debugging::MinimalStatistics& stats)
            {
                stats.lights = m_PublishedLightCount;
                stats.cmds = GetDrawCallsCount();
                stats.primitives = GetPrimitivesCount();
                stats.vertices = GetVerticesCount();
            });
        }

        ToggleMultisampling(initialSettings->multisampling);

        FramebufferSpecifications viewportSpecs;
        viewportSpecs.width = m_Settings->viewportWidth;
        viewportSpecs.height = m_Settings->viewportHeight;
        viewportSpecs.hasColor = true;
        viewportSpecs.hasDepth = true;
        viewportSpecs.multisampled = true;
        m_ViewportBuffer = Framebuffer::Create(viewportSpecs);

        FramebufferSpecifications sceneSpecs = viewportSpecs;
        sceneSpecs.colorSpecs.internalFormat = TextureInternalFormat::RGBA16F;
        m_SceneBuffer = Framebuffer::Create(sceneSpecs);

        struct BasicVertex {
            glm::vec3 position;
            glm::vec2 texCoord;
        };

        VertexLayout basicVertexLayout{
            {{"aPos",       ShaderDataType::Vec3, 0, offsetof(BasicVertex, position)},
            {"aTexCoord", ShaderDataType::Vec2, 1, offsetof(BasicVertex, texCoord)}},
            sizeof(BasicVertex)
        };
        std::vector<BasicVertex> cubeVertices = {
            {{-0.5f,-0.5f, 0.5f}, {0.0f,0.0f}},
            {{ 0.5f,-0.5f, 0.5f}, {1.0f,0.0f}},
            {{ 0.5f, 0.5f, 0.5f}, {1.0f,1.0f}},
            {{-0.5f, 0.5f, 0.5f}, {0.0f,1.0f}},

            {{ 0.5f,-0.5f,-0.5f}, {0.0f,0.0f}},
            {{-0.5f,-0.5f,-0.5f}, {1.0f,0.0f}},
            {{-0.5f, 0.5f,-0.5f}, {1.0f,1.0f}},
            {{ 0.5f, 0.5f,-0.5f}, {0.0f,1.0f}},

            {{-0.5f,-0.5f,-0.5f}, {0.0f,0.0f}},
            {{-0.5f,-0.5f, 0.5f}, {1.0f,0.0f}},
            {{-0.5f, 0.5f, 0.5f}, {1.0f,1.0f}},
            {{-0.5f, 0.5f,-0.5f}, {0.0f,1.0f}},

            {{ 0.5f,-0.5f, 0.5f}, {0.0f,0.0f}},
            {{ 0.5f,-0.5f,-0.5f}, {1.0f,0.0f}},
            {{ 0.5f, 0.5f,-0.5f}, {1.0f,1.0f}},
            {{ 0.5f, 0.5f, 0.5f}, {0.0f,1.0f}},

            {{-0.5f, 0.5f, 0.5f}, {0.0f,0.0f}},
            {{ 0.5f, 0.5f, 0.5f}, {1.0f,0.0f}},
            {{ 0.5f, 0.5f,-0.5f}, {1.0f,1.0f}},
            {{-0.5f, 0.5f,-0.5f}, {0.0f,1.0f}},

            {{-0.5f,-0.5f,-0.5f}, {0.0f,0.0f}},
            {{ 0.5f,-0.5f,-0.5f}, {1.0f,0.0f}},
            {{ 0.5f,-0.5f, 0.5f}, {1.0f,1.0f}},
            {{-0.5f,-0.5f, 0.5f}, {0.0f,1.0f}}
        };
        std::vector<uint32_t> cubeIndices = {
            0,1,2, 2,3,0,
            4,5,6, 6,7,4,
            8,9,10, 10,11,8,
            12,13,14, 14,15,12,
            16,17,18, 18,19,16,
            20,21,22, 22,23,20
        };
        m_UnitCube = Mesh::Create();
        std::vector<uint8_t> cubeVertexBuffer;
        cubeVertexBuffer.resize(cubeVertices.size() * sizeof(BasicVertex));
        memcpy(cubeVertexBuffer.data(), cubeVertices.data(), cubeVertexBuffer.size());
        m_UnitCube->Create(cubeVertexBuffer, cubeIndices, basicVertexLayout);

        std::vector<BasicVertex> quadVertices = {
            {{-1.0f,  1.0f, 0.0f}, {0.0f, 1.0f}},
            {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
            {{ 1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
            {{ 1.0f,  1.0f, 0.0f}, {1.0f, 1.0f}}
        };
        std::vector<uint32_t> quadIndices = {
            0, 1, 2,
            2, 3, 0
        };
        m_UnitQuad = Mesh::Create();
        std::vector<uint8_t> quadVertexBuffer;
        quadVertexBuffer.resize(quadVertices.size() * sizeof(BasicVertex));
        memcpy(quadVertexBuffer.data(), quadVertices.data(), quadVertexBuffer.size());
        m_UnitQuad->Create(quadVertexBuffer, quadIndices, basicVertexLayout);

        m_LightManager = std::make_shared<LightManager>();
        m_ShadowManager = std::make_shared<ShadowManager>();
        m_ShadowManager->Init(1024, 1024, 2048);
        m_ProbeManager = std::make_shared<ProbeManager>();
        m_LightmapManager = std::make_shared<LightmapManager>();

        std::shared_ptr<RenderPass> forwardPass = std::make_shared<RenderPass>();
        forwardPass->target = m_SceneBuffer;
        forwardPass->clearColor = true;
        forwardPass->clearDepth = true;
        forwardPass->overridePipeline = false;
        forwardPass->depthPrepass = true;
        forwardPass->debugViewRole = DebugViewRole::Forward;
        AddRenderPass(forwardPass, "ForwardPass", {});
        m_RendererAPI->SetClearColor(0,0,0,1);

        std::shared_ptr<RenderPass> resolvePass = std::make_shared<RenderPass>();
        resolvePass->target = m_ViewportBuffer;
        resolvePass->clearColor = false;
        resolvePass->clearDepth = false;
        resolvePass->allowResize = false;
        resolvePass->graphSideEffects = true;
        AddRenderPass(resolvePass, kResolvePassName, {"ForwardPass"});

        m_SSAOManager = std::make_shared<SSAOManager>();
        m_SSAOManager->Init(this, m_Settings->viewportWidth, m_Settings->viewportHeight);

        m_SSRManager = std::make_shared<SSRManager>();
        m_SSRManager->Init(this);

        m_LightCullingManager = std::make_shared<LightCullingManager>();
        m_LightCullingManager->Init(this);

        m_OcclusionCulling = std::make_shared<OcclusionCullingManager>();
        m_OcclusionCulling->Init(this);

        m_BillboardRenderer.Init(this);
        m_SpriteRenderer.Init(this);

        RegisterBuiltinRenderPipelines();
        {
            RenderPipelineRegistry& registry = RenderPipelineRegistry::Get();
            registry.ScanDirectory((Core::GetEngine().GetFileManager()->GetEngineResRoot() / "pipelines").GetNativePath());
            if (Core::GetEngine().GetCurrentProject())
                registry.ScanDirectory((Core::GetEngine().GetCurrentProject()->GetProjectResourcesPath() / "pipelines").GetNativePath());
        }
        ActivatePipeline(m_RequestedPipeline);

        ConnectPluginExtensions();

        m_ImmediateRenderer.Init(this);
        m_ThumbnailService.Init(this);

        Material::PreloadDefaultTextures();
    }

    std::string Renderer::GraphResourceName(const std::shared_ptr<Framebuffer>& target) const
    {
        if (!target)
            return {};
        if (target == m_ViewportBuffer)
            return kViewportResource;
        if (target == m_SceneBuffer)
            return kSceneResource;

        for (const std::string& name : m_PassInsertionOrder)
        {
            auto it = m_RenderPasses.find(name);
            if (it != m_RenderPasses.end() && it->second->target == target)
                return name + ".target";
        }

        return {};
    }

    uint64_t Renderer::ComputeGraphSignature() const
    {
        uint64_t h = 1469598103934665603ULL;
        for (const std::string& name : m_PassInsertionOrder)
        {
            auto it = m_RenderPasses.find(name);
            if (it == m_RenderPasses.end())
                continue;

            h = HashValue(h, it->second.get());
            h = HashValue(h, it->second->enabled);
        }
        return h;
    }

    void Renderer::BuildExecutionOrder()
    {
        m_ExecutionOrder.clear();
        m_ExecutionBarriers.clear();
        m_Graph.Clear();

        std::unordered_set<std::string> resourcesDeclared;

        auto declareTarget = [&](const std::shared_ptr<Framebuffer>& target, bool persistent)
        {
            const std::string name = GraphResourceName(target);
            if (name.empty())
                return name;

            if (resourcesDeclared.insert(name).second)
            {
                const FramebufferSpecifications& specs = target->GetSpecifications();

                GraphResourceDesc desc;
                desc.name = name;
                // Presenter reads the viewport and an uncached shadow map keeps its content across frames, so neither may alias memory.
                desc.imported = persistent || name == kViewportResource;
                desc.width = specs.width;
                desc.height = specs.height;
                desc.bytesPerTexel = FramebufferBytesPerTexel(specs);
                m_Graph.DeclareResource(desc);
            }
            return name;
        };

        std::unordered_map<std::string, std::vector<std::string>> dependencies = m_RenderPassDependencies;
        if (m_ActivePipeline)
            m_ActivePipeline->AdjustDependencies(dependencies);

        for (const std::string& name : m_PassInsertionOrder)
        {
            auto passIt = m_RenderPasses.find(name);
            if (passIt == m_RenderPasses.end())
                continue;

            const std::shared_ptr<RenderPass>& pass = passIt->second;

            GraphPassDesc desc;
            desc.name = name;
            desc.enabled = pass->enabled;
            desc.sideEffects = pass->graphSideEffects;

            auto depsIt = dependencies.find(name);
            if (depsIt != dependencies.end())
                desc.after = depsIt->second;

            std::unordered_set<std::string> touched;
            const std::string written = declareTarget(pass->target, pass->cacheOutput);
            if (!written.empty())
            {
                const bool hasColor = !pass->target || pass->target->GetSpecifications().hasColor;
                desc.accesses.push_back({written, hasColor ? ResourceUsage::ColorTarget : ResourceUsage::DepthTarget});
                touched.insert(written);
            }

            for (const PassAccess& access : pass->graphAccesses)
            {
                desc.accesses.push_back(access);
                touched.insert(access.resource);
            }

            for (const std::string& dependency : desc.after)
            {
                auto dependencyIt = m_RenderPasses.find(dependency);
                if (dependencyIt == m_RenderPasses.end())
                    continue;

                const std::string produced = declareTarget(dependencyIt->second->target, dependencyIt->second->cacheOutput);
                if (!produced.empty() && touched.insert(produced).second)
                    desc.accesses.push_back({produced, ResourceUsage::ShaderRead});
            }

            m_Graph.AddPass(desc);
        }

        m_GraphSignature = ComputeGraphSignature();
        CompiledGraph compiled = m_Graph.Compile();

        if (!compiled.valid)
        {
            DEBUG_ERROR("RenderPass cycle detected! " + compiled.error);
            return;
        }

        for (int index : compiled.order)
        {
            m_ExecutionOrder.push_back(compiled.passes[index].name);
            m_ExecutionBarriers.push_back(compiled.passes[index].memoryBarrierBefore);
        }

        {
            std::lock_guard<std::mutex> lock(m_GraphMutex);
            m_GraphText = RenderGraph::ToText(compiled, m_Graph);
            m_GraphDot = RenderGraph::ToDot(compiled, m_Graph);
        }

        m_CompiledGraph = std::move(compiled);
    }

    void Renderer::Render()
    {
        m_FrameLease = m_SnapshotBuffer.Acquire();

        {
            std::lock_guard<std::mutex> lock(m_DebugViewMutex);
            m_DebugView = m_PendingDebugView;
            m_DebugView.lightingTerms = m_LightingTerms.load(std::memory_order_relaxed);
        }

        m_RendererAPI->BeginFrame();

        GpuProfiler* gpu = m_RendererAPI->GetGpuProfiler();
        if (gpu)
        {
            gpu->SetEnabled(cvGpuTimers.Get());
            gpu->BeginFrame();
        }
        {
            GpuScope frameScope(gpu, "Frame");
            {
                GpuScope scope(gpu, "Managers");
                BeginFrame();
            }
            DrawFrame();
            EndFrame();
        }
        if (gpu)
            gpu->EndFrame();

        m_RendererAPI->EndFrame();

        m_FrameLease = {};
    }

    void Renderer::Shutdown()
    {
        // Features hold plugin code, release them before plugins unload.
        DisconnectPluginExtensions();

        // Scene builds run on the job system, which is destroyed right after the renderer shuts down.
        if(m_ProbeManager)
            m_ProbeManager->WaitForSceneBuilds();
        if(m_LightmapManager)
            m_LightmapManager->WaitForJobs();
        m_ThumbnailService.Shutdown();
        m_ImmediateRenderer.Shutdown();
        m_ViewportBuffer->Destroy();
        m_SceneBuffer->Destroy();
        m_LightManager->Clear();
        if(m_RendererAPI)
            m_RendererAPI->PersistCaches();
    }

    void Renderer::ClearPassesContent()
    {
        SHARD_FORWARD_TO_GRAPHICS_THREAD(ClearPassesContent())

        for(auto& [name, pass] : m_RenderPasses){
            pass->drawList.clear();
            pass->drawCommandsLookup.clear();
        }
        m_ContentEpoch.fetch_add(1, std::memory_order_release);

        shadowDrawCommandsLookup.clear();
        shadowDrawList.clear();

        m_SyncedMeshes.clear();
        m_SyncedSkyMaterial = nullptr;

        m_CommandRefCount.clear();
        m_VerticesCount = 0;
        m_PrimitivesCount = 0;
        m_DrawCallsCount = 0;

        if(m_SSAOManager)
            m_SSAOManager->SubmitFullscreenCommands(this);

        if(m_ActivePipeline)
            m_ActivePipeline->OnPassesCleared(*this);

        for(const std::shared_ptr<RenderFeature>& feature : m_Features)
            feature->OnPassesCleared(*this);
    }

    uint64_t Renderer::GenerateSortKey(const DrawCommand& cmd, const uint32_t submeshID)
    {
        uint64_t key = 0;

        uint64_t pipelineID = (uint64_t)cmd.material->GetPipeline().get();
        uint64_t materialID = cmd.material->GetAssetID().GetAsInt();
        uint64_t meshID     = cmd.mesh->GetAssetID().GetAsInt();

        pipelineID = (pipelineID >> 4) & 0xFFFF;
        materialID = (materialID >> 4) & 0xFFFF;
        meshID     = (meshID >> 4) & 0xFFFF;

        key |= (pipelineID << 48);
        key |= (materialID << 32);
        key |= (meshID << 16);
        key |= ((cmd.modelID + submeshID) & 0xFFFF);

        return key;
    }

    std::shared_ptr<Pipeline> Renderer::GetOrAddPipeline(const PipelineSpecifications& specs)
    {
        std::lock_guard<std::mutex> lock(m_PipelineMutex);
        auto [it, inserted] = m_Pipelines.try_emplace(specs, nullptr);

        if (inserted)
        {
            // Prune pipelines only the cache still holds, or they keep stale shaders alive forever.
            PruneUnusedPipelines(m_Pipelines);
            it->second = Pipeline::Create(specs);
        }

        return it->second;
    }

    std::shared_ptr<ComputePipeline> Renderer::GetOrAddComputePipeline(const ComputePipelineSpecifications& specs)
    {
        std::lock_guard<std::mutex> lock(m_PipelineMutex);
        auto [it, inserted] = m_ComputePipelines.try_emplace(specs, nullptr);

        if (inserted)
        {
            PruneUnusedPipelines(m_ComputePipelines);
            it->second = ComputePipeline::Create(specs);
        }

        return it->second;
    }

    void Renderer::DispatchCompute(const std::shared_ptr<ComputePipeline> pipeline, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ, MemoryBarrierBit barriersAfter)
    {
        DebugScope marker(m_RendererAPI.get(), pipeline && !pipeline->GetSpecifications().debugName.empty() ? pipeline->GetSpecifications().debugName.c_str() : "Compute");
        m_RendererAPI->ExecuteComputeDispatch(pipeline, groupsX, groupsY, groupsZ);

        if (barriersAfter != MemoryBarrierBit::None)
            m_RendererAPI->MemoryBarrier(barriersAfter);
    }

    void Renderer::ReorderDrawList()
    {
        auto sortAndReindex = [](std::vector<DrawCommand>& drawList, std::unordered_map<uint64_t, size_t>& lookup)
        {
            auto byKey = [](const DrawCommand& a, const DrawCommand& b){ return a.sortKey < b.sortKey; };

            if (!std::is_sorted(drawList.begin(), drawList.end(), byKey))
                std::sort(drawList.begin(), drawList.end(), byKey);

            for (size_t i = 0; i < drawList.size(); i++)
                lookup[drawList[i].commandID] = i;
        };

        for (auto& [name, pass] : m_RenderPasses)
        {
            if (!pass->externalDrawList)
                sortAndReindex(pass->drawList, pass->drawCommandsLookup);
        }

        sortAndReindex(shadowDrawList, shadowDrawCommandsLookup);
    }

    void Renderer::AddOrUpdateCommands(const std::vector<DrawCommand>& commands, const std::vector<std::string>& passes, bool addToShadowDrawList)
    {
        if (!RenderThread::Get().IsGraphicsThread())
        {
            RenderThread::Get().Post([this, commands, passes, addToShadowDrawList] { AddOrUpdateCommands(commands, passes, addToShadowDrawList); });
            return;
        }

        uint32_t newDrawCallsCount = 0;

        for(const auto& requestedPass : passes){
            auto requested = m_RenderPasses.find(requestedPass);

            if(requested != m_RenderPasses.end()){
                for(size_t submeshID = 0; submeshID < commands.size(); submeshID++)
                {
                    auto cmd = commands[submeshID];

                    const std::string passName = m_ActivePipeline ? m_ActivePipeline->Route(requestedPass, cmd) : requestedPass;
                    auto pass = passName == requestedPass ? requested : m_RenderPasses.find(passName);
                    if(pass == m_RenderPasses.end())
                        continue;
                    
                    if(!cmd.fullscreenTri && cmd.mesh)
                    {
                        cmd.commandID = MakeCommandID(cmd.mesh->GetAssetID().GetAsInt(), cmd.modelID, submeshID);
                        cmd.sortKey = GenerateSortKey(cmd, submeshID);
                    }

                    auto it = pass->second->drawCommandsLookup.find(cmd.commandID);

                    if (it != pass->second->drawCommandsLookup.end())
                    {
                        pass->second->drawList[it->second] = cmd;
                    }
                    else
                    {
                        auto& refCount = m_CommandRefCount[cmd.commandID];

                        if (refCount == 0)
                        {
                            switch(cmd.material->GetPipeline()->GetSpecifications().topology){
                                case PrimitiveTopology::Points:
                                    m_PrimitivesCount += cmd.indexCount;
                                    break;
                                case PrimitiveTopology::Lines:
                                    m_PrimitivesCount += cmd.indexCount / 2;
                                    break;
                                case PrimitiveTopology::LineStrip:
                                    m_PrimitivesCount += (cmd.indexCount >= 2) ? cmd.indexCount - 1 : 0;
                                    break;
                                case PrimitiveTopology::TriangleFan:
                                    m_PrimitivesCount += (cmd.indexCount >= 3) ? cmd.indexCount - 2 : 0;
                                    break;
                                case PrimitiveTopology::Triangles:
                                    m_PrimitivesCount += cmd.indexCount / 3;
                                    break;
                                case PrimitiveTopology::TriangleStrip:
                                    m_PrimitivesCount += (cmd.indexCount >= 3) ? cmd.indexCount - 2 : 0;
                                    break;
                            }

                            m_VerticesCount += cmd.vertexCount;
                        }

                        refCount++;

                        pass->second->drawCommandsLookup[cmd.commandID] = pass->second->drawList.size();
                        pass->second->drawList.push_back(cmd);
                        newDrawCallsCount++;
                    }
                }

            }
            else{
                DEBUG_WARNING("Tried adding a draw command to a non-registered draw pass. Always register the pass before the draw commands ! passName : ", requestedPass);
            }
        }
        m_DrawCallsCount += newDrawCallsCount;

        if(addToShadowDrawList)
        {
            for(size_t submeshID = 0; submeshID < commands.size(); submeshID++)
            {
                auto cmd = commands[submeshID];
                
                if(!cmd.fullscreenTri)
                {
                    cmd.commandID = MakeCommandID(cmd.mesh->GetAssetID().GetAsInt(), cmd.modelID, submeshID);
                    cmd.sortKey = GenerateSortKey(cmd, submeshID);


                    auto it = shadowDrawCommandsLookup.find(cmd.commandID);

                    if (it != shadowDrawCommandsLookup.end())
                    {
                        shadowDrawList[it->second] = cmd;
                    }
                    else
                    {
                        shadowDrawCommandsLookup[cmd.commandID] = shadowDrawList.size();
                        shadowDrawList.push_back(cmd);
                    }
                }
            }
        }
    }

    void Renderer::RemoveCommands(const std::vector<uint64_t> commandIDs, const std::vector<std::string>& passes, bool removeFromShadowDrawList)
    {
        if (!RenderThread::Get().IsGraphicsThread())
        {
            RenderThread::Get().Post([this, commandIDs, passes, removeFromShadowDrawList] { RemoveCommands(commandIDs, passes, removeFromShadowDrawList); });
            return;
        }

        size_t removedCount = 0;

        std::vector<std::string> removalPasses;
        for(const std::string& requestedPass : passes){
            if(m_ActivePipeline)
                m_ActivePipeline->RouteRemoval(requestedPass, removalPasses);
            else
                removalPasses.push_back(requestedPass);
        }

        for(auto passName : removalPasses){

            auto pass = m_RenderPasses.find(passName);

            if(pass != m_RenderPasses.end()){
                for(uint64_t id : commandIDs)
                {
                    auto it = pass->second->drawCommandsLookup.find(id);
                    if (it == pass->second->drawCommandsLookup.end())
                        continue;

                    size_t index = it->second;
                    size_t lastIndex = pass->second->drawList.size() - 1;

                    DrawCommand removedCmd = pass->second->drawList[index];

                    if (index != lastIndex)
                    {
                        pass->second->drawList[index] = pass->second->drawList[lastIndex];
                        pass->second->drawCommandsLookup[pass->second->drawList[index].commandID] = index;
                    }

                    auto refIt = m_CommandRefCount.find(id);
                    if (refIt != m_CommandRefCount.end())
                    {
                        refIt->second--;

                        if (refIt->second == 0)
                        {
                            const DrawCommand& cmd = removedCmd;

                            switch(cmd.material->GetPipeline()->GetSpecifications().topology){
                                case PrimitiveTopology::Points:
                                    m_PrimitivesCount -= cmd.indexCount;
                                    break;
                                case PrimitiveTopology::Lines:
                                    m_PrimitivesCount -= cmd.indexCount / 2;
                                    break;
                                case PrimitiveTopology::LineStrip:
                                    m_PrimitivesCount -= (cmd.indexCount >= 2) ? cmd.indexCount - 1 : 0;
                                    break;
                                case PrimitiveTopology::TriangleFan:
                                    m_PrimitivesCount -= (cmd.indexCount >= 3) ? cmd.indexCount - 2 : 0;
                                    break;
                                case PrimitiveTopology::Triangles:
                                    m_PrimitivesCount -= cmd.indexCount / 3;
                                    break;
                                case PrimitiveTopology::TriangleStrip:
                                    m_PrimitivesCount -= (cmd.indexCount >= 3) ? cmd.indexCount - 2 : 0;
                                    break;
                            }

                            m_VerticesCount -= cmd.vertexCount;

                            m_CommandRefCount.erase(refIt);
                        }
                    }

                    pass->second->drawList.pop_back();
                    pass->second->drawCommandsLookup.erase(it);

                    removedCount++;

                }
            }
            else{
                DEBUG_WARNING("Tried removing a draw command to a non-registered draw pass. Always register the pass before removing the draw commands : passName : ", passName);
            }

        }

        m_DrawCallsCount = (removedCount > m_DrawCallsCount) ? 0 : (m_DrawCallsCount - (uint32_t)removedCount);

        if(removeFromShadowDrawList)
        {
            for(uint64_t id : commandIDs)
                {
                    auto it = shadowDrawCommandsLookup.find(id);
                    if (it == shadowDrawCommandsLookup.end())
                        continue;

                    size_t index = it->second;
                    size_t lastIndex = shadowDrawList.size() - 1;

                    if (index != lastIndex)
                    {
                        shadowDrawList[index] = shadowDrawList[lastIndex];
                        shadowDrawCommandsLookup[shadowDrawList[index].commandID] = index;
                    }

                    shadowDrawList.pop_back();
                    shadowDrawCommandsLookup.erase(it);
                }
        }
    }

    void Renderer::AddRenderPass(const std::shared_ptr<RenderPass> pass, const std::string& name, const std::vector<std::string>& dependencies)
    {
        SHARD_FORWARD_TO_GRAPHICS_THREAD(AddRenderPass(pass, name, dependencies))

        if (!pass)
        {
            DEBUG_ERROR("Trying to add null RenderPass: " + name);
            return;
        }

        if(m_RenderPasses.find(name) == m_RenderPasses.end()){
            m_PassInsertionOrder.push_back(name);
        }

        m_RenderPasses[name] = pass;

        m_RenderPassDependencies[name] = dependencies;

        m_NeedExecutionOrderRebuild = true;
    }

    void Renderer::RemoveRenderPass(const std::string &name)
    {
        SHARD_FORWARD_TO_GRAPHICS_THREAD(RemoveRenderPass(name))

        auto it = m_RenderPasses.find(name);

        if(it != m_RenderPasses.end()){
            m_RenderPasses.erase(name);
            m_PassInsertionOrder.erase(std::remove(m_PassInsertionOrder.begin(), m_PassInsertionOrder.end(), name), m_PassInsertionOrder.end());
        }
        else{
            DEBUG_ERROR("Tried removing non-registered render pass");
        }

        for(auto& r : m_RenderPasses)
        {
            RemoveDependencyFromPass(r.first, name);
        }
        
        m_NeedExecutionOrderRebuild = true;
    }

    void Renderer::AddDependencyToPass(const std::string& passName, const std::string& dependencyName)
    {
        SHARD_FORWARD_TO_GRAPHICS_THREAD(AddDependencyToPass(passName, dependencyName))

        auto passIt = m_RenderPasses.find(passName);
        if (passIt == m_RenderPasses.end())
        {
            DEBUG_ERROR("Tried adding dependency to non-existent pass: " + passName);
            return;
        }

        m_RenderPassDependencies[passName].push_back(dependencyName);
        m_NeedExecutionOrderRebuild = true;
    }

    void Renderer::RemoveDependencyFromPass(const std::string& passName, const std::string& dependencyName)
    {
        SHARD_FORWARD_TO_GRAPHICS_THREAD(RemoveDependencyFromPass(passName, dependencyName))

        auto passIt = m_RenderPasses.find(passName);
        if (passIt == m_RenderPasses.end())
        {
            DEBUG_ERROR("Tried removing dependency from non-existent pass: " + passName);
            return;
        }

        for(size_t i = 0; i < m_RenderPassDependencies[passName].size(); i++){
            if(m_RenderPassDependencies[passName][i] == dependencyName)
            {
                m_RenderPassDependencies[passName].erase(m_RenderPassDependencies[passName].begin() + i);
            }
        }
        m_NeedExecutionOrderRebuild = true;
    }

    void Renderer::RescaleFramebuffers(int newWidth, int newHeight)
    {
        SHARD_FORWARD_TO_GRAPHICS_THREAD(RescaleFramebuffers(newWidth, newHeight))

        for(auto renderPass : m_RenderPasses){
            if(renderPass.second->allowResize){
                renderPass.second->target->Resize(newWidth, newHeight);
            }
        }

        for(const auto& buffer : {m_ViewportBuffer, m_SceneBuffer}){
            if(buffer && (buffer->GetWidth() != uint32_t(newWidth) || buffer->GetHeight() != uint32_t(newHeight)))
                buffer->Resize(newWidth, newHeight);
        }

        if(m_SSAOManager)
            m_SSAOManager->RefreshSamplers(this);

        if(onViewportResized)
            onViewportResized(newWidth, newHeight);
    }

    void Renderer::ToggleMultisampling(const bool on)
    {
        SHARD_FORWARD_TO_GRAPHICS_THREAD(ToggleMultisampling(on))

        m_MultisamplingEnabled = on;
        m_RendererAPI->ToggleMultisampling(on);
    }

    void Renderer::SetMultisampleCount(uint32_t samples)
    {
        SHARD_FORWARD_TO_GRAPHICS_THREAD(SetMultisampleCount(samples))

        for(const auto& buffer : {m_ViewportBuffer, m_SceneBuffer})
            if(buffer)
                buffer->SetSamplesCount(samples);
    }

    void Renderer::SetShadowSettings(const Projects::ShadowSettings& settings)
    {
        SHARD_FORWARD_TO_GRAPHICS_THREAD(SetShadowSettings(settings))

        m_ShadowManager->Configure(settings);
    }

    std::string Renderer::DumpFrame(const std::string& directory)
    {
        return OnGraphicsThread([&]() -> std::string {
            std::error_code ec;
            std::filesystem::create_directories(directory, ec);
            if (ec)
                return "cannot create " + directory;

            std::string report;

            {
                std::ofstream graph(std::filesystem::path(directory) / "graph.txt", std::ios::binary);
                graph << GetRenderGraphText();
                std::ofstream dot(std::filesystem::path(directory) / "graph.dot", std::ios::binary);
                dot << GetRenderGraphDot();
                report += "graph.txt graph.dot";
            }

            std::unordered_set<const Framebuffer*> written;
            auto dump = [&](const std::string& name, const std::shared_ptr<Framebuffer>& target) {
                if (!target || !target->GetSpecifications().hasColor || !written.insert(target.get()).second)
                    return;

                if (target->GetSpecifications().multisampled)
                    target->ResolveMultisampled();

                std::vector<uint8_t> pixels;
                if (!target->ReadPixelsRGBA8(pixels))
                    return;

                const std::string file = name + ".png";
                if (ImageExport::WritePNG(Filesystem::Path(directory + "/" + file), target->GetWidth(), target->GetHeight(), 4, pixels.data()))
                    report += " " + file;
            };

            auto dumpDepth = [&](const std::string& name, const std::shared_ptr<Framebuffer>& target) {
                if (!target || target->GetSpecifications().hasColor || !target->HasDepthTarget() || !written.insert(target.get()).second)
                    return;

                std::vector<float> depth;
                if (!target->ReadDepthFloat(depth) || depth.empty())
                    return;

                float low = 1.0f, high = 0.0f;
                for (float d : depth)
                    if (d < 0.9999f) { low = std::min(low, d); high = std::max(high, d); }
                const float range = std::max(high - low, 1e-9f);

                std::vector<uint8_t> pixels(depth.size() * 4);
                for (size_t i = 0; i < depth.size(); i++)
                {
                    const uint8_t v = depth[i] >= 0.9999f ? 255 : uint8_t(std::clamp((depth[i] - low) / range, 0.0f, 1.0f) * 220.0f);
                    pixels[i * 4 + 0] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = v;
                    pixels[i * 4 + 3] = 255;
                }

                const std::string file = name + "_depth.png";
                if (ImageExport::WritePNG(Filesystem::Path(directory + "/" + file), target->GetWidth(), target->GetHeight(), 4, pixels.data()))
                    report += " " + file + " (depth " + std::to_string(low) + " .. " + std::to_string(high) + ")";
            };

            dump("Viewport", m_ViewportBuffer);
            dump("SceneHDR", m_SceneBuffer);

            for (const std::string& name : m_ExecutionOrder)
            {
                auto it = m_RenderPasses.find(name);
                if (it != m_RenderPasses.end())
                {
                    dump(name, it->second->target);
                    dumpDepth(name, it->second->target);
                }
            }

            return report;
        });
    }

    bool Renderer::WriteScreenshot(const std::string& path, std::string& error)
    {
        const std::string extension = Filesystem::Path(path).GetExtensionString();
        const bool hdr = extension == ".hdr";
        if (!hdr && extension != ".png")
        {
            error = "the file has to be a .hdr (the scene in linear light) or a .png (the picture as shown)";
            return false;
        }

        return OnGraphicsThread([&]() -> bool {
            const std::shared_ptr<Framebuffer> target = hdr ? m_SceneBuffer : m_ViewportBuffer;
            if (!target)
            {
                error = "nothing was drawn yet";
                return false;
            }
            if (target->GetSpecifications().multisampled)
                target->ResolveMultisampled();

            const uint32_t width = target->GetWidth();
            const uint32_t height = target->GetHeight();
            const size_t pixels = size_t(width) * height;

            bool written = false;
            if (hdr)
            {
                std::vector<float> rgba;
                if (!target->ReadPixelsRGBAFloat(rgba) || rgba.size() < pixels * 4)
                {
                    error = "the scene target cannot be read back in floating point on this graphics API";
                    return false;
                }

                std::vector<float> rgb(pixels * 3);
                for (uint32_t y = 0; y < height; y++)
                    for (uint32_t x = 0; x < width; x++)
                        for (uint32_t c = 0; c < 3; c++)
                            rgb[(size_t(height - 1 - y) * width + x) * 3 + c] = rgba[(size_t(y) * width + x) * 4 + c];
                written = ImageExport::WriteHDR(Filesystem::Path(path), width, height, 3, rgb.data());
            }
            else
            {
                std::vector<uint8_t> rgba;
                if (!target->ReadPixelsRGBA8(rgba) || rgba.size() < pixels * 4)
                {
                    error = "the viewport cannot be read back";
                    return false;
                }

                const size_t rowBytes = size_t(width) * 4;
                for (size_t y = 0; y < height / 2; y++)
                    std::swap_ranges(rgba.begin() + y * rowBytes, rgba.begin() + (y + 1) * rowBytes, rgba.begin() + (height - 1 - y) * rowBytes);
                written = ImageExport::WritePNG(Filesystem::Path(path), width, height, 4, rgba.data());
            }

            if (!written)
                error = "cannot write " + path;
            return written;
        });
    }

    std::string Renderer::GetDeviceVendor()
    {
        return OnGraphicsThread([this] { return m_RendererAPI->GetDeviceVendor(); });
    }

    std::string Renderer::GetRendererName()
    {
        return OnGraphicsThread([this] { return m_RendererAPI->GetRendererName(); });
    }

    std::string Renderer::GetDriverVersion()
    {
        return OnGraphicsThread([this] { return m_RendererAPI->GetDriverVersion(); });
    }

    namespace {

        const std::vector<std::string> kScenePasses = {"ForwardPass", "SSAODepthNormalPass"};

        // Fixed key so every skybox recreation updates the same draw command in place and can remove it.
        uint64_t SkyboxCommandID()
        {
            constexpr uint64_t kSkyboxComponentID = 0x534B59;
            return MakeCommandID(0, kSkyboxComponentID, 0);
        }

        std::vector<uint64_t> MeshCommandIDs(Mesh& mesh, int componentId)
        {
            std::vector<uint64_t> ids;
            for(size_t i = 0; i < mesh.GetSubMeshes().size(); i++)
                ids.push_back(MakeCommandID(mesh.GetAssetID().GetAsInt(), componentId, (uint64_t)i));
            return ids;
        }
    }

    void Renderer::RemoveSyncedMeshCommands(const SyncedMesh& synced)
    {
        if(!synced.mesh)
            return;

        const std::vector<uint64_t> ids = MeshCommandIDs(*synced.mesh, synced.componentId);
        RemoveCommands(ids, kScenePasses, true);
        if(!synced.extraPasses.empty())
            RemoveCommands(ids, synced.extraPasses, false);
    }

    void Renderer::SyncSceneDrawLists()
    {
        const RenderSnapshot& snapshot = GetSnapshot();
        m_SyncCount++;

        for(const MeshProxy& proxy : snapshot.meshes)
        {
            auto [it, inserted] = m_SyncedMeshes.try_emplace(proxy.key);
            SyncedMesh& synced = it->second;
            synced.seenInSync = m_SyncCount;

            const uint64_t lightmapHandle = m_LightmapManager->GetPageHandle(proxy.lightmapPage);

            const bool unchanged = !inserted && synced.mesh == proxy.mesh && synced.materials == proxy.materials &&
                                   synced.worldMatrix == proxy.worldMatrix && synced.extraPasses == proxy.extraPasses &&
                                   synced.objectId == proxy.objectId && synced.componentId == proxy.componentId &&
                                   synced.lightmapHandle == lightmapHandle && synced.lightmapScaleOffset == proxy.lightmapScaleOffset;
            if(unchanged)
                continue;

            if(!inserted && (synced.mesh != proxy.mesh || synced.extraPasses != proxy.extraPasses || synced.componentId != proxy.componentId))
                RemoveSyncedMeshCommands(synced);

            std::vector<DrawCommand> commands = proxy.mesh->CreateDrawCommands(proxy.worldMatrix, proxy.objectId, proxy.componentId, proxy.materials,
                                                                               lightmapHandle, proxy.lightmapScaleOffset);

            // SSAO prepass always runs and needs every mesh's real geometry. ssaoEnabled only gates the shading.
            AddOrUpdateCommands(commands, kScenePasses, true);
            if(!proxy.extraPasses.empty())
                AddOrUpdateCommands(commands, proxy.extraPasses, false);

            synced.mesh = proxy.mesh;
            synced.materials = proxy.materials;
            synced.worldMatrix = proxy.worldMatrix;
            synced.extraPasses = proxy.extraPasses;
            synced.objectId = proxy.objectId;
            synced.componentId = proxy.componentId;
            synced.lightmapHandle = lightmapHandle;
            synced.lightmapScaleOffset = proxy.lightmapScaleOffset;
        }

        for(auto it = m_SyncedMeshes.begin(); it != m_SyncedMeshes.end();)
        {
            if(it->second.seenInSync != m_SyncCount)
            {
                RemoveSyncedMeshCommands(it->second);
                it = m_SyncedMeshes.erase(it);
            }
            else
                ++it;
        }

        SyncSkyboxDrawCommand();
    }

    void Renderer::SyncSkyboxDrawCommand()
    {
        const EnvironmentProxy& environment = GetSnapshot().environment;

        if(!environment.skyboxMaterial)
        {
            if(m_SyncedSkyMaterial)
            {
                RemoveCommands({SkyboxCommandID()}, {"ForwardPass"}, false);
                m_SyncedSkyMaterial = nullptr;
            }
            return;
        }

        if(m_SyncedSkyMaterial == environment.skyboxMaterial && m_SyncedSkyObjectId == environment.skyboxObjectId)
            return;

        DrawCommand cmd{};
        cmd.commandID = SkyboxCommandID();
        cmd.objectID = environment.skyboxObjectId;
        cmd.modelMatrix = glm::mat4(1.0f);
        cmd.mesh = nullptr;
        cmd.indexCount = 3;
        cmd.indexOffset = 0;
        cmd.vertexCount = 3;
        cmd.material = environment.skyboxMaterial;
        cmd.fullscreenTri = true;
        AddOrUpdateCommands({cmd}, {"ForwardPass"}, false);

        m_SyncedSkyMaterial = environment.skyboxMaterial;
        m_SyncedSkyObjectId = environment.skyboxObjectId;
    }

    void Renderer::SetRenderPipeline(const std::string& name)
    {
        {
            std::lock_guard<std::mutex> lock(m_PipelineSelectMutex);
            if(!RenderPipelineRegistry::Get().Has(name))
            {
                DEBUG_ERROR("There is no render pipeline called '", name, "'.");
                return;
            }
            m_RequestedPipeline = name;
        }
    }

    void Renderer::AddRenderFeature(const std::shared_ptr<RenderFeature>& feature)
    {
        if(!feature)
            return;
        RenderThread::Get().InvokeResult([&] {
            if(std::find(m_Features.begin(), m_Features.end(), feature) != m_Features.end())
                return;
            m_Features.push_back(feature);
            feature->Activate(*this);
            m_NeedExecutionOrderRebuild = true;
            DEBUG_INFO("Render feature : ", feature->GetName());
        });
    }

    void Renderer::RemoveRenderFeature(const std::shared_ptr<RenderFeature>& feature)
    {
        if(!feature)
            return;
        RenderThread::Get().InvokeResult([&] {
            auto it = std::find(m_Features.begin(), m_Features.end(), feature);
            if(it == m_Features.end())
                return;
            m_Features.erase(it);
            feature->Deactivate(*this);
            m_NeedExecutionOrderRebuild = true;
        });
    }

    std::vector<std::string> Renderer::GetRenderFeatureNames() const
    {
        std::vector<std::string> names;
        for(const std::shared_ptr<RenderFeature>& f : m_Features)
            names.push_back(f->GetName());
        return names;
    }

    void Renderer::OnPipelineTypeRemoved(const std::string& type)
    {
        RenderThread::Get().InvokeResult([&] {
            const bool wasActive = m_ActivePipeline && type == m_ActivePipeline->GetType();
            bool wasRequested;
            {
                std::lock_guard<std::mutex> lock(m_PipelineSelectMutex);
                wasRequested = m_RequestedPipeline == type;
                if(wasRequested)
                    m_RequestedPipeline = "forward+";
            }
            if(wasActive)
                ActivatePipeline("forward+");
        });
    }

    void Renderer::ConnectPluginExtensions()
    {
        Plugins::ExtensionRegistry& extensions = Plugins::PluginManager::Get().Extensions();
        m_FeatureListener = extensions.Listen("render.feature", [this](const Plugins::ExtensionRegistry::Item& item, bool added) {
            if(item.typeName != typeid(RenderFeature).name())
                return;
            auto feature = std::static_pointer_cast<RenderFeature>(item.object);
            if(added)
                AddRenderFeature(feature);
            else
                RemoveRenderFeature(feature);
        });
        m_PipelineListener = extensions.Listen("render.pipeline", [this](const Plugins::ExtensionRegistry::Item& item, bool added) {
            if(item.typeName != typeid(RenderPipelineExtension).name())
                return;
            auto pipeline = std::static_pointer_cast<RenderPipelineExtension>(item.object);
            if(added){
                RenderPipelineRegistry::Get().RegisterType(pipeline->type, pipeline->description, pipeline->factory);
            }
            else{
                RenderPipelineRegistry::Get().UnregisterType(pipeline->type);
                OnPipelineTypeRemoved(pipeline->type);
            }
        });
    }

    void Renderer::DisconnectPluginExtensions()
    {
        Plugins::ExtensionRegistry& extensions = Plugins::PluginManager::Get().Extensions();
        if(m_FeatureListener){
            extensions.Unlisten(m_FeatureListener);
            m_FeatureListener = 0;
        }
        if(m_PipelineListener){
            extensions.Unlisten(m_PipelineListener);
            m_PipelineListener = 0;
        }
        std::vector<std::shared_ptr<RenderFeature>> features = m_Features;
        for(const std::shared_ptr<RenderFeature>& feature : features)
            RemoveRenderFeature(feature);
    }

    void Renderer::ActivatePipeline(const std::string& name)
    {
        if(m_ActivePipeline)
        {
            m_ActivePipeline->Deactivate(*this);
            m_ActivePipeline.reset();
        }

        std::unique_ptr<RenderPipeline> pipeline = RenderPipelineRegistry::Get().Create(name);
        std::string resolved = name;
        if(!pipeline)
        {
            DEBUG_ERROR("The render pipeline '", name, "' could not be created : using forward+.");
            pipeline = RenderPipelineRegistry::Get().Create("forward+");
            resolved = "forward+";
        }

        m_ActivePipeline = std::move(pipeline);
        {
            std::lock_guard<std::mutex> lock(m_PipelineSelectMutex);
            m_ActivePipelineName = resolved;
        }

        m_ActivePipeline->Activate(*this);

        m_SyncedMeshes.clear();
        m_SyncedSkyMaterial = nullptr;
        if(auto forward = m_RenderPasses.find("ForwardPass"); forward != m_RenderPasses.end())
        {
            forward->second->drawList.clear();
            forward->second->drawCommandsLookup.clear();
        }
        m_CommandRefCount.clear();
        m_NeedExecutionOrderRebuild = true;

        DEBUG_INFO("Render pipeline : ", resolved);
    }

    void Renderer::ApplyPipelineForFrame()
    {
        std::string wanted;
        {
            std::lock_guard<std::mutex> lock(m_PipelineSelectMutex);
            wanted = m_RequestedPipeline;
        }

        std::string effective = wanted;
        if(wanted != "forward+")
        {
            bool supportsView = true;
            if(m_ActivePipeline && wanted == m_ActivePipelineName)
                supportsView = m_ActivePipeline->SupportsView(m_DebugView);
            else if(std::unique_ptr<RenderPipeline> candidate = RenderPipelineRegistry::Get().Create(wanted))
                supportsView = candidate->SupportsView(m_DebugView);

            if(!supportsView)
                effective = "forward+";
        }

        if(effective != m_ActivePipelineName)
            ActivatePipeline(effective);
    }

    void Renderer::BeginFrame()
    {
        ApplyPipelineForFrame();

        m_LightmapManager->Sync(GetSnapshot());
        SyncSceneDrawLists();
        m_BillboardRenderer.Sync(GetSnapshot());
        m_SpriteRenderer.Sync(GetSnapshot());
        m_LightManager->Sync(GetSnapshot().lights);
        m_PublishedLightCount = m_LightManager->GetLightsCount();
        m_PublishedPassCount = static_cast<uint32_t>(m_RenderPasses.size());
        m_ProbeManager->Sync(GetSnapshot());

        ReorderDrawList();

        {
            SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::ShadowUpdate);
            m_ShadowManager->UpdatePassUniforms();
        }

        {
            SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::GIProbeUpdate);
            m_ProbeManager->Update();
            m_ProbeManager->PublishStatus();
        }

        m_LightmapManager->Update();
        m_LightmapManager->PublishStatus();

        {
            SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::SSAOUpdate);
            m_SSAOManager->Update();
            m_SSRManager->Update();
        }

        if(m_ActivePipeline)
            m_ActivePipeline->BeforeFrame(*this, GetSnapshot());

        for(const std::shared_ptr<RenderFeature>& feature : m_Features)
            feature->BeforeFrame(*this, GetSnapshot());

        for(auto& [name, pass] : m_RenderPasses)
        {
            if(pass->debugViewRole == DebugViewRole::WireOverlay)
                pass->enabled = m_DebugView.mode == ViewMode::ShadedWireframe;
        }

        {
            SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::LightCullingUpdate);
            m_LightCullingManager->Update();
        }

        if(m_NeedExecutionOrderRebuild || ComputeGraphSignature() != m_GraphSignature){
            BuildExecutionOrder();
            m_NeedExecutionOrderRebuild = false;
        }
    }

    void Renderer::DrawFrame()
    {
        std::unordered_map<const std::vector<DrawCommand>*, uint64_t> drawListHashes;

        DrawPluginOverlays(true);

        for (size_t orderIndex = 0; orderIndex < m_ExecutionOrder.size(); orderIndex++)
        {
            const std::string& passName = m_ExecutionOrder[orderIndex];
            auto passIt = m_RenderPasses.find(passName);
            if (passIt == m_RenderPasses.end())
                continue;

            auto pass = passIt->second;
            if (!pass->enabled)
                continue;

            if (m_ExecutionBarriers[orderIndex] != MemoryBarrierBit::None)
                m_RendererAPI->MemoryBarrier(m_ExecutionBarriers[orderIndex]);

            if (passName == kResolvePassName)
            {
                GpuScope passScope(m_RendererAPI->GetGpuProfiler(), passName.c_str());
                DebugScope passMarker(m_RendererAPI.get(), passName.c_str());
                ResolveSceneToViewport();
                continue;
            }

            if (passName == SSRManager::kPassName)
            {
                // Reprograms API state behind the draw path's back. With partial lighting terms, keep the reflections of the last frame that drew them all.
                if (m_DebugView.lightingTerms != LightingTermsAll)
                    continue;

                GpuScope passScope(m_RendererAPI->GetGpuProfiler(), passName.c_str());
                DebugScope passMarker(m_RendererAPI.get(), passName.c_str());
                m_SSRManager->Execute(m_SceneBuffer->GetResolveColorAttachment());
                m_RendererAPI->InvalidateStateCache();
                continue;
            }

            const bool canCache = !m_ViewStack.empty() || GetSnapshot().camera.valid;

            if (pass->cacheOutput && !canCache)
                pass->hasCachedState = false;

            if (pass->cacheOutput && canCache)
            {
                const std::vector<DrawCommand>* list = pass->externalDrawList ? pass->externalDrawList : &pass->drawList;

                auto hashIt = drawListHashes.find(list);
                if (hashIt == drawListHashes.end())
                    hashIt = drawListHashes.emplace(list, HashDrawList(*list)).first;

                const uint64_t stateHash = HashPassState(*pass, hashIt->second);

                if (pass->hasCachedState && pass->cachedStateHash == stateHash)
                    continue;

                pass->cachedStateHash = stateHash;
                pass->hasCachedState = true;
            }

            const bool debugViewIsPlain = m_DebugView.mode == ViewMode::Lit || m_DebugView.mode == ViewMode::Unlit;
            const bool sceneGeometryPass = pass->debugViewRole == DebugViewRole::Forward || pass->debugViewRole == DebugViewRole::GBuffer;
            const bool occlusionCulled = (passName == "SSAODepthNormalPass" || (sceneGeometryPass && debugViewIsPlain))
                && PrepareOcclusionCulling(passName, pass);

            GpuScope passScope(m_RendererAPI->GetGpuProfiler(), passName.c_str());
            DebugScope passMarker(m_RendererAPI.get(), passName.c_str());

            BeginRenderPass(pass);

            if (pass->debugViewRole == DebugViewRole::GBuffer || pass->debugViewRole == DebugViewRole::Lighting)
            {
                DebugViewState state = m_DebugView;
                if (state.mode == ViewMode::ShadedWireframe)
                    state.mode = ViewMode::Lit;
                state.wireframeRaster = pass->debugViewRole == DebugViewRole::GBuffer && state.mode == ViewMode::Wireframe;

                m_SkipFullscreenCommands = (!debugViewIsPlain && m_DebugView.mode != ViewMode::ShadedWireframe)
                    || (m_DebugView.lightingTerms & LightingTermEmission) == 0;
                m_RendererAPI->SetDebugView(state);
                if (occlusionCulled)
                    ExecuteOcclusionCulledPass(passName, pass);
                else
                    ExecuteRenderPass();

                m_SkipFullscreenCommands = false;
                m_RendererAPI->SetDebugView(DebugViewState{});
            }
            else if (pass->debugViewRole == DebugViewRole::WireOverlay)
            {
                DebugViewState state = m_DebugView;
                state.mode = ViewMode::ShadedWireframe;
                state.wireframeRaster = true;
                m_SkipFullscreenCommands = true;
                m_RendererAPI->SetDebugView(state);
                ExecuteRenderPass();

                m_SkipFullscreenCommands = false;
                m_RendererAPI->SetDebugView(DebugViewState{});
            }
            else if (pass->debugViewRole == DebugViewRole::Forward)
            {
                DebugViewState state = m_DebugView;
                m_SkipFullscreenCommands = false;

                switch (state.mode)
                {
                    case ViewMode::Wireframe:
                        state.wireframeRaster = true;
                        m_SkipFullscreenCommands = true;
                        m_RendererAPI->SetDebugView(state);
                        ExecuteRenderPass();
                        break;

                    case ViewMode::ShadedWireframe:
                        state.mode = ViewMode::Lit;
                        m_RendererAPI->SetDebugView(state);
                        ExecuteRenderPass();

                        m_RendererAPI->InvalidateStateCache();
                        state.mode = ViewMode::ShadedWireframe;
                        state.wireframeRaster = true;
                        m_SkipFullscreenCommands = true;
                        m_RendererAPI->SetDebugView(state);
                        ExecuteRenderPass();
                        break;

                    case ViewMode::Normals:
                    case ViewMode::Depth:
                    case ViewMode::UVs:
                    case ViewMode::GlobalIllumination:
                    case ViewMode::SSAO:
                    case ViewMode::LightmapUVs:
                    case ViewMode::ShadowCascades:
                        m_SkipFullscreenCommands = true;
                        m_RendererAPI->SetDebugView(state);
                        ExecuteRenderPass();
                        break;

                    default:
                        m_SkipFullscreenCommands = (state.lightingTerms & LightingTermEmission) == 0;
                        m_RendererAPI->SetDebugView(state);
                        if (occlusionCulled)
                            ExecuteOcclusionCulledPass(passName, pass);
                        else
                            ExecuteRenderPass();
                        break;
                }

                m_SkipFullscreenCommands = false;
                m_RendererAPI->SetDebugView(DebugViewState{});
            }
            else if (occlusionCulled)
            {
                ExecuteOcclusionCulledPass(passName, pass);
            }
            else
            {
                ExecuteRenderPass();
            }

            EndRenderPass();
        }

        DrawPluginOverlays(false);
    }

    void Renderer::DrawPluginOverlays(bool intoTextures)
    {
        const RenderSnapshot& snapshot = GetSnapshot();
        if (snapshot.overlays.empty())
            return;
        for (const OverlayProxy& overlay : snapshot.overlays)
        {
            if ((overlay.target != nullptr) != intoTextures)
                continue;
            std::shared_ptr<RenderPass> pass;
            if (overlay.target)
            {
                pass = std::make_shared<RenderPass>();
                pass->target = overlay.target;
            }
            else
            {
                if (!m_ViewportBuffer)
                    continue;
                if (!m_OverlayPass)
                {
                    m_OverlayPass = std::make_shared<RenderPass>();
                    m_OverlayPass->target = m_ViewportBuffer;
                }
                pass = m_OverlayPass;
                pass->target = m_ViewportBuffer;
            }
            pass->clearColor = false;
            pass->clearDepth = false;
            pass->allowResize = false;
            pass->allowCulling = false;
            BeginRenderPass(pass);
            m_RendererAPI->DrawOverlay(overlay, pass->target->GetWidth(), pass->target->GetHeight());
            EndRenderPass();
        }
        m_CurrentPass = nullptr;
    }

    void Renderer::EnsureTonemapPass()
    {
        if (m_TonemapPass)
            return;

        const Filesystem::Path shaderDirectory = Core::GetEngine().GetFileManager()->GetEngineResRoot() / "shaders/fb";
        m_TonemapShader = Shader::Create(shaderDirectory / "tonemap.vert", shaderDirectory / "tonemap.frag", Filesystem::Path(""));
        if (!m_TonemapShader)
        {
            DEBUG_ERROR("The tonemap shader could not be loaded : the viewport shows the HDR scene as it is.");
            return;
        }

        PipelineSpecifications specs;
        specs.shader = m_TonemapShader;
        specs.topology = PrimitiveTopology::Triangles;
        specs.cullMode = CullMode::None;
        specs.depthTest = false;
        specs.depthWrite = false;
        specs.blending = false;
        specs.debugName = "TonemapPipeline";

        m_TonemapMaterial = Material::Create(m_TonemapShader, GetOrAddPipeline(specs), false, Opacity::Opaque);

        m_TonemapPass = std::make_shared<RenderPass>();
        m_TonemapPass->target = m_ViewportBuffer;
        m_TonemapPass->clearColor = false;
        m_TonemapPass->clearDepth = false;
        m_TonemapPass->allowResize = false;
        m_TonemapPass->allowCulling = false;
    }

    int Renderer::GetDisplayMode() const
    {
        const DebugViewState& view = m_DebugView;
        switch (view.mode)
        {
            case ViewMode::Lit:
                return view.showLighting ? 0 : 1;
            case ViewMode::ShadedWireframe:
            case ViewMode::GlobalIllumination:
                return 0;

            case ViewMode::Unlit:
                return 1;

            default:
                return 2;
        }
    }

    void Renderer::ResolveSceneToViewport()
    {
        EnsureTonemapPass();

        {
            SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::MultisampleResolve);

            m_SceneBuffer->ResolveMultisampled();

            if (!m_TonemapPass)
            {
                m_ViewportBuffer->CopyFrom(m_SceneBuffer);
                return;
            }

            m_ViewportBuffer->CopyDepthFrom(m_SceneBuffer);
        }

        const DisplaySettings& display = GetSnapshot().environment.display;
        const FramebufferSpecifications& sceneSpecs = m_SceneBuffer->GetSpecifications();
        const int sceneSamples = sceneSpecs.multisampled ? static_cast<int>(sceneSpecs.samplesCount) : 0;
        m_TonemapMaterial->SetTextureParameter("sceneTexture", m_SceneBuffer->GetResolveColorAttachment());
        m_TonemapMaterial->SetTextureParameter("sceneTextureMS", sceneSamples > 0 ? m_SceneBuffer->GetColorAttachment() : 0);
        m_TonemapMaterial->SetScalarParameter("sceneSamples", sceneSamples);
        m_TonemapMaterial->SetScalarParameter("exposure", display.ExposureScale());
        m_TonemapMaterial->SetScalarParameter("tonemapper", static_cast<int>(display.tonemapper));
        m_TonemapMaterial->SetScalarParameter("displayMode", GetDisplayMode());

        DrawCommand command{};
        command.commandID = 0x544F4E45;
        command.mesh = nullptr;
        command.material = m_TonemapMaterial;
        command.indexCount = 3;
        command.vertexCount = 3;
        command.fullscreenTri = true;
        command.bindCameraState = false;

        BeginRenderPass(m_TonemapPass);
        m_RendererAPI->ExecuteDrawCommand(command, m_TonemapPass);
        EndRenderPass();
    }

    void Renderer::EndFrame()
    {
        SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::MultisampleResolve);
        m_ViewportBuffer->ResolveMultisampled();
    }

    void Renderer::BeginRenderPass(const std::shared_ptr<RenderPass>& pass)
    {
        SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::PassSetup);

        m_CurrentPass = pass;
        pass->target->Bind();

        m_RendererAPI->SetViewport(0, 0, pass->target->GetWidth(), pass->target->GetHeight());
        ClearBit clearMask = ClearBit::None;
        if(pass->clearColor)
            clearMask |= ClearBit::Color;
        if(pass->clearDepth)
            clearMask |= ClearBit::Depth;
        m_RendererAPI->Clear(clearMask);
    }

    bool Renderer::GetCurrentView(RenderView& out)
    {
        if(!m_ViewStack.empty())
        {
            out = m_ViewStack.back();
            return true;
        }

        return BuildMainView(GetSnapshot(), out);
    }

    void Renderer::RenderImmediate(const std::shared_ptr<RenderPass>& pass, const RenderView& view)
    {
        PushView(view);
        BeginRenderPass(pass);
        ExecuteRenderPass();
        EndRenderPass();
        PopView();
        m_CurrentPass = nullptr;
    }

    void Renderer::ExecuteRenderPass()
    {
        const CameraProxy* camera = nullptr;
        if(m_ViewStack.empty())
        {
            camera = &GetSnapshot().camera;

            // No active camera is normal right after boot or during a world swap. Warn only on the first missing frame.
            static bool hadCameraLastCall = true;
            if(!camera->valid)
            {
                if(hadCameraLastCall)
                    DEBUG_WARNING("No active camera - skipping render passes until one is set");
                hadCameraLastCall = false;
                return;
            }
            hadCameraLastCall = true;
        }

        // Frustum is the active camera's, so pushed views are never culled. Shadow passes name their own matrix (cullMatrixUniform).
        bool cullingActive = false;
        FrustumPlanes frustumPlanes;

        if (camera && camera->frustumCulling && m_CurrentPass->allowCulling)
        {
            cullingActive = true;
            frustumPlanes = ExtractFrustumPlanes(CullMatrix(*camera));
        }
        else if (!m_CurrentPass->allowCulling && !m_CurrentPass->cullMatrixUniform.empty())
        {
            auto it = m_CurrentPass->customUniforms.find(m_CurrentPass->cullMatrixUniform);
            if (it != m_CurrentPass->customUniforms.end())
            {
                if (const glm::mat4* matrix = std::get_if<glm::mat4>(&it->second))
                {
                    cullingActive = true;
                    frustumPlanes = ExtractFrustumPlanes(*matrix);
                }
            }
        }

        m_VisibleCommands.clear();

        for(auto& drawCmd : (m_CurrentPass->externalDrawList ? *m_CurrentPass->externalDrawList : m_CurrentPass->drawList)){

            bool skip = false;

            {
                SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::Culling);

                // Fullscreen-triangle commands have no index buffer, so only require indexCount for indexed draws.
                if (!drawCmd.material || (!drawCmd.fullscreenTri && drawCmd.indexCount <= 0))
                {
                    skip = true;
                }
                else if (cullingActive)
                {
                    glm::vec3 center = (drawCmd.boundsMin + drawCmd.boundsMax) * 0.5f;
                    glm::vec3 extents = (drawCmd.boundsMax - drawCmd.boundsMin) * 0.5f;

                    glm::vec3 worldCenter = glm::vec3(drawCmd.modelMatrix * glm::vec4(center, 1.0));

                    glm::mat3 m = glm::mat3(drawCmd.modelMatrix);

                    glm::mat3 absM(
                        glm::abs(m[0]),
                        glm::abs(m[1]),
                        glm::abs(m[2])
                    );

                    glm::vec3 worldExtents = absM * extents;

                    glm::vec3 worldMin = worldCenter - worldExtents;
                    glm::vec3 worldMax = worldCenter + worldExtents;

                    if (worldMin != worldMax && !AABBInFrustum(frustumPlanes, worldMin, worldMax))
                        skip = true;
                }
            }

            if (skip || (m_SkipFullscreenCommands && drawCmd.fullscreenTri))
                continue;

            if (m_OcclusionPhase != 0 && drawCmd.cullSlot < 0)
            {
                const bool translucent = !drawCmd.fullscreenTri && drawCmd.material->GetOpacity() == Opacity::Translucent;
                if (translucent != (m_OcclusionPhase == 2))
                    continue;
            }

            m_VisibleCommands.push_back(&drawCmd);
        }

        const bool depthSweeps = cvDepthPrepass.Get() && m_CurrentPass->depthPrepass && m_ViewStack.empty()
            && m_DebugView.mode == ViewMode::Lit && !m_DebugView.wireframeRaster && !m_SkipFullscreenCommands;

        if (depthSweeps)
        {
            GpuScope scope(m_RendererAPI->GetGpuProfiler(), "  depth sweep");
            m_RendererAPI->SetDepthSweep(DepthSweep::Prepass);
            for (const DrawCommand* drawCmd : m_VisibleCommands)
                if (m_RendererAPI->IsDepthPrepassCandidate(*drawCmd, *m_CurrentPass))
                    m_RendererAPI->ExecuteDrawCommand(*drawCmd, m_CurrentPass);
            m_RendererAPI->SetDepthSweep(DepthSweep::Equal);
        }

        {
            GpuScope scope(m_RendererAPI->GetGpuProfiler(), depthSweeps ? "  shaded sweep" : "  draws");
            for (const DrawCommand* drawCmd : m_VisibleCommands)
                m_RendererAPI->ExecuteDrawCommand(*drawCmd, m_CurrentPass);
        }

        if (depthSweeps)
            m_RendererAPI->SetDepthSweep(DepthSweep::None);
    }

    OcclusionCullingManager::Target Renderer::MakeOcclusionTarget(const std::shared_ptr<RenderPass>& pass) const
    {
        const FramebufferSpecifications& specs = pass->target->GetSpecifications();

        OcclusionCullingManager::Target target;
        target.depthTexture = pass->target->GetDepthAttachment();
        target.multisampled = specs.multisampled;
        target.samples = specs.multisampled ? specs.samplesCount : 1;
        target.width = pass->target->GetWidth();
        target.height = pass->target->GetHeight();
        return target;
    }

    bool Renderer::PrepareOcclusionCulling(const std::string& passName, const std::shared_ptr<RenderPass>& pass)
    {
        if (!m_OcclusionCullingEnabled || !m_OcclusionCulling || !m_OcclusionCulling->IsReady())
            return false;

        if (!m_ViewStack.empty() || pass->externalDrawList || !pass->allowCulling || !pass->target || !pass->target->GetSpecifications().hasDepth)
            return false;

        const CameraProxy& camera = GetSnapshot().camera;
        if (!camera.valid || !camera.frustumCulling)
            return false;

        return m_OcclusionCulling->PrepareFirstPhase(passName, pass->drawList, CullMatrix(camera), MakeOcclusionTarget(pass));
    }

    void Renderer::ExecuteOcclusionCulledPass(const std::string& passName, const std::shared_ptr<RenderPass>& pass)
    {
        const CameraProxy& camera = GetSnapshot().camera;
        if (!camera.valid)
            return;

        m_RendererAPI->SetIndirectDrawBuffer(m_OcclusionCulling->GetIndirectBuffer(passName));

        m_OcclusionPhase = 1;
        ExecuteRenderPass();

        // Hi-Z from phase 1 depth, then re-test every command. Compute reprograms API state, so invalidate before drawing.
        m_OcclusionCulling->PrepareSecondPhase(passName, CullMatrix(camera), MakeOcclusionTarget(pass));
        m_RendererAPI->InvalidateStateCache();

        m_OcclusionPhase = 2;
        ExecuteRenderPass();

        m_OcclusionPhase = 0;
        m_RendererAPI->SetIndirectDrawBuffer(0);
    }

    void Renderer::EndRenderPass()
    {
        if (m_CurrentPass->barrierAfter != MemoryBarrierBit::None)
            m_RendererAPI->MemoryBarrier(m_CurrentPass->barrierAfter);
    }
}
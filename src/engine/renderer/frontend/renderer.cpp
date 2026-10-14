#include "renderer.hpp"
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
#include "engine/renderer/features/lighting/light_culling_manager.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/renderer/frontend/render_asset_kinds.hpp"
#include "engine/core/diagnostics/profiler.hpp"
#include "engine/renderer/rhi/frustum.hpp"
#include <queue>
#include "engine/renderer/features/culling/occlusion_culling_manager.hpp"
#include <algorithm>
#include <glm/gtx/string_cast.hpp>

#include "engine/core/config/cvar.hpp"

namespace Shard::Engine::Rendering{

    namespace {

        Core::CVar<bool> cvDepthPrepass("render.depth_prepass", true, "Lay the depth of the opaque geometry down first so every pixel is shaded once (see render.gpu)");
        Core::CVar<bool> cvGpuTimers("render.gpu_timers", false, "Measure the GPU time of each render pass (see render.gpu)");

        // FNV-1a over raw bytes, chained through `h`. Only used to detect "did anything this pass depends on
        // change since it last ran" (RenderPass::cacheOutput), so a collision merely costs a stale frame and
        // -0.0f vs 0.0f merely costs a redundant redraw.
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

    }








    void Renderer::Init(std::shared_ptr<RendererSettings> initialSettings)
    {
        m_Settings = initialSettings;

        // Before anything below asks the resources manager for a shader or a texture
        RegisterAssetKinds(*Core::GetEngine().GetResourcesManager());

        m_RendererAPI = RendererAPI::Create(initialSettings->api);
        RendererAPI::SetCurrent(m_RendererAPI.get());
        m_RendererAPI->SetSceneBinding(this);

        // Textures are block compressed when they are loaded : a GPU that cannot take them gets them uncompressed
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

        //Init viewport framebuffers
        FramebufferSpecifications viewportSpecs;
        viewportSpecs.width = m_Settings->viewportWidth;
        viewportSpecs.height = m_Settings->viewportHeight;
        viewportSpecs.hasColor = true;
        viewportSpecs.hasDepth = true;
        viewportSpecs.multisampled = true;
        m_ViewportBuffer = Framebuffer::Create(viewportSpecs);

        // The scene is rendered in HDR : the forward pass draws here, and the viewport above only ever receives the result
        FramebufferSpecifications sceneSpecs = viewportSpecs;
        sceneSpecs.colorSpecs.internalFormat = TextureInternalFormat::RGBA16F;
        m_SceneBuffer = Framebuffer::Create(sceneSpecs);

        //Init base geometry
        /// Unit cube
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
            // Front
            {{-0.5f,-0.5f, 0.5f}, {0.0f,0.0f}},
            {{ 0.5f,-0.5f, 0.5f}, {1.0f,0.0f}},
            {{ 0.5f, 0.5f, 0.5f}, {1.0f,1.0f}},
            {{-0.5f, 0.5f, 0.5f}, {0.0f,1.0f}},

            // Back
            {{ 0.5f,-0.5f,-0.5f}, {0.0f,0.0f}},
            {{-0.5f,-0.5f,-0.5f}, {1.0f,0.0f}},
            {{-0.5f, 0.5f,-0.5f}, {1.0f,1.0f}},
            {{ 0.5f, 0.5f,-0.5f}, {0.0f,1.0f}},

            // Left
            {{-0.5f,-0.5f,-0.5f}, {0.0f,0.0f}},
            {{-0.5f,-0.5f, 0.5f}, {1.0f,0.0f}},
            {{-0.5f, 0.5f, 0.5f}, {1.0f,1.0f}},
            {{-0.5f, 0.5f,-0.5f}, {0.0f,1.0f}},

            // Right
            {{ 0.5f,-0.5f, 0.5f}, {0.0f,0.0f}},
            {{ 0.5f,-0.5f,-0.5f}, {1.0f,0.0f}},
            {{ 0.5f, 0.5f,-0.5f}, {1.0f,1.0f}},
            {{ 0.5f, 0.5f, 0.5f}, {0.0f,1.0f}},

            // Top
            {{-0.5f, 0.5f, 0.5f}, {0.0f,0.0f}},
            {{ 0.5f, 0.5f, 0.5f}, {1.0f,0.0f}},
            {{ 0.5f, 0.5f,-0.5f}, {1.0f,1.0f}},
            {{-0.5f, 0.5f,-0.5f}, {0.0f,1.0f}},

            // Bottom
            {{-0.5f,-0.5f,-0.5f}, {0.0f,0.0f}},
            {{ 0.5f,-0.5f,-0.5f}, {1.0f,0.0f}},
            {{ 0.5f,-0.5f, 0.5f}, {1.0f,1.0f}},
            {{-0.5f,-0.5f, 0.5f}, {0.0f,1.0f}}
        };
        std::vector<uint32_t> cubeIndices = {
            0,1,2, 2,3,0,       // Front
            4,5,6, 6,7,4,       // Back
            8,9,10, 10,11,8,    // Left
            12,13,14, 14,15,12, // Right
            16,17,18, 18,19,16, // Top
            20,21,22, 22,23,20  // Bottom
        };
        m_UnitCube = Mesh::Create();
        std::vector<uint8_t> cubeVertexBuffer;
        cubeVertexBuffer.resize(cubeVertices.size() * sizeof(BasicVertex));
        memcpy(cubeVertexBuffer.data(), cubeVertices.data(), cubeVertexBuffer.size());
        m_UnitCube->Create(cubeVertexBuffer, cubeIndices, basicVertexLayout);

        /// Unit quad
        std::vector<BasicVertex> quadVertices = {
            {{-1.0f,  1.0f, 0.0f}, {0.0f, 1.0f}}, // top-left
            {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}}, // bottom-left
            {{ 1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}}, // bottom-right
            {{ 1.0f,  1.0f, 0.0f}, {1.0f, 1.0f}}  // top-right
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

        // Init lighting
        m_LightManager = std::make_shared<LightManager>();
        m_ShadowManager = std::make_shared<ShadowManager>();
        m_ShadowManager->Init(1024, 1024, 2048);
        m_ProbeManager = std::make_shared<ProbeManager>();
        m_LightmapManager = std::make_shared<LightmapManager>();

        // Init built-in passes
        /// Forward pass
        std::shared_ptr<RenderPass> forwardPass = std::make_shared<RenderPass>();
        forwardPass->target = m_SceneBuffer;
        forwardPass->clearColor = true;
        forwardPass->clearDepth = true;
        forwardPass->overridePipeline = false;
        forwardPass->depthPrepass = true;
        // Initially no dependencies for forward pass
        AddRenderPass(forwardPass, "ForwardPass", {});
        m_RendererAPI->SetClearColor(0,0,0,1);

        m_SSAOManager = std::make_shared<SSAOManager>();
        m_SSAOManager->Init(this, m_Settings->viewportWidth, m_Settings->viewportHeight);

        // Init Forward+ light culling
        m_LightCullingManager = std::make_shared<LightCullingManager>();
        m_LightCullingManager->Init(this);

        m_OcclusionCulling = std::make_shared<OcclusionCullingManager>();
        m_OcclusionCulling->Init(this);

        m_BillboardRenderer.Init(this);

        m_ImmediateRenderer.Init(this);
        m_ThumbnailService.Init(this);

        Material::PreloadDefaultTextures();
    }

    void Renderer::BuildExecutionOrder()
    {
        m_ExecutionOrder.clear();

        std::unordered_map<std::string, int> inDegree;
        std::unordered_map<std::string, std::vector<std::string>> dependents;

        for (const auto& [name, _] : m_RenderPasses)
        {
            inDegree[name] = 0;
            dependents[name] = {};
        }

        for (const auto& [name, deps] : m_RenderPassDependencies)
        {
            for (const auto& dep : deps)
            {
                inDegree[name]++;
                dependents[dep].push_back(name);
            }
        }
 
        auto getCategory = [](const std::string& name, const std::unordered_map<std::string, std::vector<std::string>>& dependencies, const std::unordered_map<std::string, std::vector<std::string>>& dependents) {
            auto it = dependencies.find(name);
            bool hasDeps = (it != dependencies.end() && !it->second.empty());
            bool hasDependents = !dependents.at(name).empty();
        
            if (!hasDeps && !hasDependents) return 0;
            if (!hasDeps && hasDependents)  return 1;
            if (hasDeps && hasDependents)   return 2;
            return 3; // hasDeps && !hasDependents
        };

        auto cmp = [getCategory, this, &dependents](const std::string& a, const std::string& b) {
            int ca = getCategory(a, this->m_RenderPassDependencies, dependents);
            int cb = getCategory(b, this->m_RenderPassDependencies, dependents);

            if (ca != cb)
                return ca > cb; // lower category first

            return a > b;
        };

        std::priority_queue<
            std::string,
            std::vector<std::string>,
            decltype(cmp)
        > q(cmp);

        for (const auto& [name, _] : m_RenderPasses)
        {
            if (inDegree[name] == 0)
                q.push(name);
        }

        std::vector<std::string> topo;

        while (!q.empty())
        {
            auto current = q.top();
            q.pop();

            topo.push_back(current);

            for (const auto& dep : dependents[current])
            {
                if (--inDegree[dep] == 0)
                    q.push(dep);
            }
        }

        // Cycle check
        if (topo.size() != m_RenderPasses.size())
        {
            DEBUG_ERROR("RenderPass cycle detected!");
            return;
        }
        
        m_ExecutionOrder = topo;
            
    }

    void Renderer::Render()
    {
        // The frame renders from the snapshot published by the Extract stage, whatever the simulation does meanwhile
        m_FrameLease = m_SnapshotBuffer.Acquire();

        {
            std::lock_guard<std::mutex> lock(m_DebugViewMutex);
            m_DebugView = m_PendingDebugView;
        }

        // A backend without GPU timers (the null one) has no profiler : GpuScope takes a null one
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

        m_FrameLease = {};
    }

    void Renderer::Shutdown()
    {
        // Scene builds run on the job system, which is destroyed right after the renderer shuts down
        if(m_ProbeManager)
            m_ProbeManager->WaitForSceneBuilds();
        if(m_LightmapManager)
            m_LightmapManager->WaitForJobs();
        m_ThumbnailService.Shutdown();
        m_ImmediateRenderer.Shutdown();
        m_ViewportBuffer->Destroy();
        m_SceneBuffer->Destroy();
        m_LightManager->Clear();
    }

    void Renderer::ClearPassesContent()
    {
        SHARD_FORWARD_TO_GRAPHICS_THREAD(ClearPassesContent())

        for(auto& [name, pass] : m_RenderPasses){
            pass->drawList.clear();
            pass->drawCommandsLookup.clear();
        }

        shadowDrawCommandsLookup.clear();
        shadowDrawList.clear();

        // What the scene had submitted is gone : the next sync submits everything the snapshot holds again
        m_SyncedMeshes.clear();
        m_SyncedSkyMaterial = nullptr;

        m_CommandRefCount.clear();
        m_VerticesCount = 0;
        m_PrimitivesCount = 0;
        m_DrawCallsCount = 0;

        // World-owned content is gone, but renderer-owned passes (SSAO's full-screen raw/blur draws) had
        // their only draw command wiped along with it - put those back.
        if(m_SSAOManager)
            m_SSAOManager->SubmitFullscreenCommands(this);
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
        key |= (cmd.modelID + submeshID & 0xFFFF);

        return key;
    }

    std::shared_ptr<Pipeline> Renderer::GetOrAddPipeline(const PipelineSpecifications& specs)
    {
        std::lock_guard<std::mutex> lock(m_PipelineMutex);
        auto [it, inserted] = m_Pipelines.try_emplace(specs, nullptr);

        if (inserted)
        {
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
            it->second = ComputePipeline::Create(specs);
        }

        return it->second;
    }

    void Renderer::DispatchCompute(const std::shared_ptr<ComputePipeline> pipeline, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ, MemoryBarrierBit barriersAfter)
    {
        m_RendererAPI->ExecuteComputeDispatch(pipeline, groupsX, groupsY, groupsZ);

        if (barriersAfter != MemoryBarrierBit::None)
            m_RendererAPI->MemoryBarrier(barriersAfter);
    }

    void Renderer::ReorderDrawList()
    {
        auto sortAndReindex = [](std::vector<DrawCommand>& drawList, std::unordered_map<uint64_t, size_t>& lookup)
        {
            std::sort(drawList.begin(), drawList.end(), [](const DrawCommand& a, const DrawCommand& b){
                return a.sortKey < b.sortKey;
            });

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
            // Fire and forget : copies of the arguments travel, and the commands are in place before the next frame is drawn
            RenderThread::Get().Post([this, commands, passes, addToShadowDrawList] { AddOrUpdateCommands(commands, passes, addToShadowDrawList); });
            return;
        }

        uint32_t newDrawCallsCount = 0;

        for(const auto& passName : passes){
            auto pass = m_RenderPasses.find(passName);

            if(pass != m_RenderPasses.end()){
                for(int submeshID = 0; submeshID < commands.size(); submeshID++)
                {
                    auto cmd = commands[submeshID];
                    
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
                DEBUG_WARNING("Tried adding a draw command to a non-registered draw pass. Always register the pass before the draw commands ! passName : ", passName);
            }
        }
        m_DrawCallsCount += newDrawCallsCount;

        if(addToShadowDrawList)
        {
            for(int submeshID = 0; submeshID < commands.size(); submeshID++)
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

        for(auto passName : passes){

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

        // Replace or insert
        m_RenderPasses[name] = pass;

        // Set dependencies
        m_RenderPassDependencies[name] = dependencies;

        m_NeedExecutionOrderRebuild = true;
    }

    void Renderer::RemoveRenderPass(const std::string &name)
    {
        SHARD_FORWARD_TO_GRAPHICS_THREAD(RemoveRenderPass(name))

        auto it = m_RenderPasses.find(name);

        if(it != m_RenderPasses.end()){
            m_RenderPasses.erase(name);
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

        // Add the dependency
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

        // Remove the dependency
        for(int i = 0; i < m_RenderPassDependencies[passName].size(); i++){
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

        // The two targets of the picture follow the viewport, whichever passes happened to resize them above (a target that
        // is no pass's own - the viewport, now that the forward pass draws in the HDR one - would never be)
        for(const auto& buffer : {m_ViewportBuffer, m_SceneBuffer}){
            if(buffer && (buffer->GetWidth() != uint32_t(newWidth) || buffer->GetHeight() != uint32_t(newHeight)))
                buffer->Resize(newWidth, newHeight);
        }

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

        // The skybox is the only fullscreen triangle the scene submits : a fixed key lets every (re)creation update
        // the same draw command in place, and lets it be removed
        uint64_t SkyboxCommandID()
        {
            constexpr uint64_t kSkyboxComponentID = 0x534B59; // "SKY"
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

            // Where the model's baked lighting is : the page may be loaded later than the mesh is drawn, or be replaced by a new bake
            const uint64_t lightmapHandle = m_LightmapManager->GetPageHandle(proxy.lightmapPage);

            const bool unchanged = !inserted && synced.mesh == proxy.mesh && synced.materials == proxy.materials &&
                                   synced.worldMatrix == proxy.worldMatrix && synced.extraPasses == proxy.extraPasses &&
                                   synced.objectId == proxy.objectId && synced.componentId == proxy.componentId &&
                                   synced.lightmapHandle == lightmapHandle && synced.lightmapScaleOffset == proxy.lightmapScaleOffset;
            if(unchanged)
                continue;

            // Another mesh or other extra passes : what the old state submitted would be left behind (the command IDs
            // derive from the mesh, and a pass the mesh left must lose it)
            if(!inserted && (synced.mesh != proxy.mesh || synced.extraPasses != proxy.extraPasses || synced.componentId != proxy.componentId))
                RemoveSyncedMeshCommands(synced);

            std::vector<DrawCommand> commands = proxy.mesh->CreateDrawCommands(proxy.worldMatrix, proxy.objectId, proxy.componentId, proxy.materials,
                                                                               lightmapHandle, proxy.lightmapScaleOffset);

            // SSAO's depth+normal prepass needs every mesh's real geometry, the same way ForwardPass does : it always runs,
            // only the world's ssaoEnabled says whether the shading uses the result
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

        // Whatever the snapshot no longer has is gone from the world (destroyed, deactivated, its world unloaded)
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

    void Renderer::BeginFrame()
    {
        // First : the draws below take the handle of the lightmap page they read from here
        m_LightmapManager->Sync(GetSnapshot());
        SyncSceneDrawLists();
        m_BillboardRenderer.Sync(GetSnapshot());
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
        }

        {
            SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::LightCullingUpdate);
            m_LightCullingManager->Update();
        }

        if(m_NeedExecutionOrderRebuild){
            BuildExecutionOrder();
            m_NeedExecutionOrderRebuild = false;
        }
    }

    void Renderer::DrawFrame()
    {
        // Hash of each external draw list (the shadow list is shared by every shadow pass), computed at most
        // once per frame and only if a cacheable pass actually asks for it.
        std::unordered_map<const std::vector<DrawCommand>*, uint64_t> drawListHashes;

        for (const auto& passName : m_ExecutionOrder)
        {
            auto pass = m_RenderPasses[passName];
            if (!pass->enabled)
                continue;

            // Without a camera (or a pushed view) ExecuteRenderPass() draws nothing, so there is no result worth
            // remembering : keep rendering (just the clear) and forget any cached state until one exists.
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
                    continue; // nothing this pass depends on changed : its target still holds last frame's result

                pass->cachedStateHash = stateHash;
                pass->hasCachedState = true;
            }

            // Phase 1 of the GPU occlusion culling is recorded before the pass starts : it only needs the draw list,
            // and the pass's own clear/bind then also resets the API state the compute dispatches disturbed.
            // The main view's debug modes (wireframe, normals...) draw everything, so they are never culled.
            const bool debugViewIsPlain = m_DebugView.mode == ViewMode::Lit || m_DebugView.mode == ViewMode::Unlit;
            const bool occlusionCulled = (passName == "SSAODepthNormalPass" || (passName == "ForwardPass" && debugViewIsPlain))
                && PrepareOcclusionCulling(passName, pass);

            GpuScope passScope(m_RendererAPI->GetGpuProfiler(), passName.c_str());

            BeginRenderPass(pass);

            if (passName == "ForwardPass")
            {
                // Debug views only ever touch the main scene render. Backgrounds (fullscreen
                // triangles, i.e. the skybox) are dropped from every mode that isn't a filled lit/unlit view.
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

                        // Second sweep over the same list as lines. Reset() so per-pass uniforms and
                        // polygon mode are re-applied for the new state.
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
                        m_SkipFullscreenCommands = true;
                        m_RendererAPI->SetDebugView(state);
                        ExecuteRenderPass();
                        break;

                    default:
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

            // The scene was drawn in HDR : the picture is made now, so that whatever is drawn over the forward pass (the grid,
            // the outlines, the billboards, the debug shapes) lands on display colours
            if (passName == "ForwardPass")
                ResolveSceneToViewport();
        }

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

        // Draws into the viewport on top of nothing : every pixel is written, the depth copied from the scene stays
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
            // Light : exposure, tonemapper, sRGB curve
            case ViewMode::Lit:
                return view.showLighting ? 0 : 1;           // without the lighting it is the albedo, flat : just its colours
            case ViewMode::ShadedWireframe:
            case ViewMode::GlobalIllumination:
                return 0;

            // The albedo alone : colours, nothing to roll off
            case ViewMode::Unlit:
                return 1;

            // Colours chosen to be seen as they are : a normal, a depth, a UV, an occlusion term, a wireframe
            default:
                return 2;
        }
    }

    void Renderer::ResolveSceneToViewport()
    {
        EnsureTonemapPass();

        // Only the resolve is this sub-sample : the tonemap draw below is made of the pass setup, state binding and draw
        // sub-samples, which a scope around it would count twice
        {
            SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::MultisampleResolve);

            m_SceneBuffer->ResolveMultisampled();

            if (!m_TonemapPass)
            {
                // No shader : show the scene as it is, so that the viewport is not left empty
                m_ViewportBuffer->CopyFrom(m_SceneBuffer);
                return;
            }

            m_ViewportBuffer->CopyDepthFrom(m_SceneBuffer);
        }

        const DisplaySettings& display = GetSnapshot().environment.display;
        // The samples themselves go to the shader (it tonemaps each one, see tonemap.frag) ; the resolved image is for a scene
        // that was not multisampled
        const FramebufferSpecifications& sceneSpecs = m_SceneBuffer->GetSpecifications();
        const int sceneSamples = sceneSpecs.multisampled ? static_cast<int>(sceneSpecs.samplesCount) : 0;
        m_TonemapMaterial->SetTextureParameter("sceneTexture", m_SceneBuffer->GetResolveColorAttachment());
        m_TonemapMaterial->SetTextureParameter("sceneTextureMS", sceneSamples > 0 ? m_SceneBuffer->GetColorAttachment() : 0);
        m_TonemapMaterial->SetScalarParameter("sceneSamples", sceneSamples);
        m_TonemapMaterial->SetScalarParameter("exposure", display.ExposureScale());
        m_TonemapMaterial->SetScalarParameter("tonemapper", static_cast<int>(display.tonemapper));
        m_TonemapMaterial->SetScalarParameter("displayMode", GetDisplayMode());

        DrawCommand command{};
        command.commandID = 0x544F4E45;     // "TONE"
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
        // With a pushed view the active camera is irrelevant (and may not even exist, e.g. a
        // thumbnail rendered while no world is loaded).
        const CameraProxy* camera = nullptr;
        if(m_ViewStack.empty())
        {
            camera = &GetSnapshot().camera;

            // Benign transient: right after boot (before the editor builds its viewport camera) or
            // during a world swap there can be a frame with no active camera. Skip the pass, and warn
            // only on the no-camera -> still-no-camera edge so a genuine "stuck" state stays visible
            // without spamming one line per pass per frame.
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

        // The camera's frustum only describes the active camera's view, so a pushed view is never culled.
        // A pass that can't use it (a shadow pass : the light's view volume is what matters) may name its own
        // view-projection uniform instead - see RenderPass::cullMatrixUniform.
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

        // What this pass draws now : culled once, drawn once (or twice with depth sweeps)
        m_VisibleCommands.clear();

        for(auto& drawCmd : (m_CurrentPass->externalDrawList ? *m_CurrentPass->externalDrawList : m_CurrentPass->drawList)){

            bool skip = false;

            {
                SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::Culling);

                // A fullscreenTri command has no index buffer (DrawFullScreenTriangle() just issues
                // glDrawArrays(GL_TRIANGLES, 0, 3)), so it never has indexCount set - only require it
                // for indexed draws, or every fullscreen-triangle command (SSAO's raw/blur passes,
                // the editor's outline composite) would get silently dropped here every frame.
                if (!drawCmd.material || (!drawCmd.fullscreenTri && drawCmd.indexCount <= 0))
                {
                    skip = true;
                }
                else if (cullingActive)
                {
                    // Bounds/frustum math is only needed to feed IsInFrustum, so skip it entirely
                    // when culling isn't active for this camera/pass.
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

            // Occlusion culled pass : the GPU-culled commands are issued in both phases (through their indirect
            // command, which says whether each one draws in that phase). Everything else is issued once -
            // translucent geometry last, so it is blended over all the opaque geometry of both phases.
            if (m_OcclusionPhase != 0 && drawCmd.cullSlot < 0)
            {
                const bool translucent = !drawCmd.fullscreenTri && drawCmd.material->GetOpacity() == Opacity::Translucent;
                if (translucent != (m_OcclusionPhase == 2))
                    continue;
            }

            m_VisibleCommands.push_back(&drawCmd);
        }

        // Opaque geometry first lays its depth down alone (cheap : no shading), then is shaded where that depth says it
        // is visible : the lit shader is by far the heaviest part of the frame and would otherwise run for every
        // surface behind another one. Only the main scene's forward pass, in its plain shaded view.
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

        // Only the active camera's own passes : a pushed view (thumbnails, studio renders) or a shared/external list
        // is some other view of the world, and a pass that opted out of camera culling wants everything drawn.
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

        // Phase 1 : what was visible last frame. This lays down the depth the pyramid is built from.
        m_OcclusionPhase = 1;
        ExecuteRenderPass();

        // Hi-Z from that depth, then every command is re-tested against it. The compute work reprograms the
        // API state behind the draw path's back, hence the invalidation before drawing again.
        m_OcclusionCulling->PrepareSecondPhase(passName, CullMatrix(camera), MakeOcclusionTarget(pass));
        m_RendererAPI->InvalidateStateCache();

        // Phase 2 : whatever turned out visible that phase 1 didn't draw
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
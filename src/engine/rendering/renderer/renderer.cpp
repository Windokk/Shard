#include "renderer.hpp"

#include "engine/core/engine.hpp"

#include "engine/rendering/material/material.hpp"
#include "engine/rendering/pipeline/pipeline.hpp"
#include "engine/rendering/pipeline/compute_pipeline.hpp"
#include "engine/rendering/shader/shader.hpp"
#include "engine/rendering/mesh/mesh.hpp"
#include "engine/rendering/lighting/shadow_manager.hpp"
#include "engine/rendering/lighting/probe_manager.hpp"
#include "engine/rendering/lighting/ssao_manager.hpp"
#include "engine/rendering/lighting/light_culling_manager.hpp"
#include "engine/rendering/camera/camera_manager.hpp"
#include "engine/core/resources/resources_manager.hpp"
#include "engine/objects/actors/actor.hpp"
#include "engine/debugging/profiler.hpp"
#include <queue>
#include <algorithm>
#include <glm/gtx/string_cast.hpp>

namespace Shard::Engine::Rendering{

    void Renderer::Init(std::shared_ptr<RendererSettings> initialSettings)
    {
        m_Settings = initialSettings;
        m_RendererAPI = RendererAPI::Create(initialSettings->api);

        ToggleMultisampling(initialSettings->multisampling);

        //Init viewport framebuffers
        FramebufferSpecifications viewportSpecs;
        viewportSpecs.width = m_Settings->viewportWidth;
        viewportSpecs.height = m_Settings->viewportHeight;
        viewportSpecs.hasColor = true;
        viewportSpecs.hasDepth = true;
        viewportSpecs.multisampled = true;
        m_ViewportBuffer = Framebuffer::Create(viewportSpecs);

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

        // Init built-in passes
        /// Forward pass
        std::shared_ptr<RenderPass> forwardPass = std::make_shared<RenderPass>();
        forwardPass->target = m_ViewportBuffer;
        forwardPass->clearColor = true;
        forwardPass->clearDepth = true;
        forwardPass->overridePipeline = false;
        // Initially no dependencies for forward pass
        AddRenderPass(forwardPass, "ForwardPass", {});
        m_RendererAPI->SetClearColor(0,0,0,1);

        std::shared_ptr<RenderPass> probeGizmoPass = std::make_shared<RenderPass>();
        probeGizmoPass->target = m_ViewportBuffer;
        probeGizmoPass->clearColor = false;
        probeGizmoPass->clearDepth = false;
        probeGizmoPass->overridePipeline = false;
        AddRenderPass(probeGizmoPass, "ProbeGizmoPass", {"ForwardPass"});

        std::shared_ptr<RenderPass> physicsDebugPass = std::make_shared<RenderPass>();
        physicsDebugPass->target = m_ViewportBuffer;
        physicsDebugPass->clearColor = false;
        physicsDebugPass->clearDepth = false;
        physicsDebugPass->overridePipeline = false;
        AddRenderPass(physicsDebugPass, "PhysicsDebugPass", {"ForwardPass"});

        m_SSAOManager = std::make_shared<SSAOManager>();
        m_SSAOManager->Init(this, m_Settings->viewportWidth, m_Settings->viewportHeight);

        // Init Forward+ light culling
        m_LightCullingManager = std::make_shared<LightCullingManager>();
        m_LightCullingManager->Init(this);

        m_ImmediateRenderer.Init(this);
        m_ThumbnailService.Init(this);

        struct Vertex {
            glm::vec3 position;
            glm::vec2 texCoord;
            glm::vec3 normal;
            glm::vec4 color;
        };

        // Init debug shapes pipeline
        Rendering::VertexLayout vertexLayout = {{{"aPos", Rendering::ShaderDataType::Vec3, 0, offsetof(Vertex, position)},{"aTexCoord", Rendering::ShaderDataType::Vec2, 1, offsetof(Vertex, texCoord)},
                                                {"aNormal", Rendering::ShaderDataType::Vec3, 2, offsetof(Vertex, normal)}, {"aColor", Rendering::ShaderDataType::Vec4, 3, offsetof(Vertex, color)}}, 
                                                    sizeof(Vertex)};

        Rendering::PipelineSpecifications pipelineSpecs;
        pipelineSpecs.blending = false;
        pipelineSpecs.cullMode = Rendering::CullMode::None;
        pipelineSpecs.debugName = "PhysicsDebug";
        pipelineSpecs.polygonMode = Rendering::PolygonMode::Line;
        pipelineSpecs.topology = Rendering::PrimitiveTopology::Lines;
        pipelineSpecs.shader = Core::GetEngine().GetResourcesManager()->GetShader("shaders/mesh/unlit");
        pipelineSpecs.vertexLayout = vertexLayout;

        std::shared_ptr<Rendering::Pipeline> pipeline = Core::GetEngine().GetRenderer()->GetOrAddPipeline(pipelineSpecs);

        m_DebugMat = Rendering::Material::Create(Core::GetEngine().GetResourcesManager()->GetShader("shaders/mesh/unlit"), pipeline, false, Rendering::Opacity::Opaque);
        m_DebugMat->SetScalarParameter("useTexture", false);
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
        BeginFrame();
        DrawFrame();
        EndFrame();
    }

    void Renderer::Shutdown()
    {
        m_ThumbnailService.Shutdown();
        m_ImmediateRenderer.Shutdown();
        m_ViewportBuffer->Destroy();
        m_LightManager->Clear();
    }

    void Renderer::ClearPassesContent()
    {
        for(auto& [name, pass] : m_RenderPasses){
            pass->drawList.clear();
            pass->drawCommandsLookup.clear();
        }

        shadowDrawCommandsLookup.clear();
        shadowDrawList.clear();

        m_CommandRefCount.clear();
        m_VerticesCount = 0;
        m_PrimitivesCount = 0;
        m_DrawCallsCount = 0;

        // Level-owned content is gone, but renderer-owned passes (SSAO's full-screen raw/blur draws) had
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
        auto [it, inserted] = m_Pipelines.try_emplace(specs, nullptr);

        if (inserted)
        {
            it->second = Pipeline::Create(specs);
        }

        return it->second;
    }

    std::shared_ptr<ComputePipeline> Renderer::GetOrAddComputePipeline(const ComputePipelineSpecifications& specs)
    {
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
        for(auto renderPass : m_RenderPasses){
            if(renderPass.second->allowResize){
                renderPass.second->target->Resize(newWidth, newHeight);
            }
        }

        Core::GetEngine().GetCameraManager()->UpdateSize(newWidth, newHeight);
    }

    void Renderer::ToggleMultisampling(const bool on)
    {
        m_MultisamplingEnabled = on;
        m_RendererAPI->ToggleMultisampling(on);
    }

    std::string Renderer::GetDeviceVendor()
    {
        return m_RendererAPI->GetDeviceVendor();
    }

    std::string Renderer::GetRendererName()
    {
        return m_RendererAPI->GetRendererName();
    }

    std::string Renderer::GetDriverVersion()
    {
        return m_RendererAPI->GetDriverVersion();
    }

    void Renderer::BeginFrame()
    {
        {
            SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::CameraUpdate);
            Core::GetEngine().GetCameraManager()->Tick();
        }

        ReorderDrawList();

        {
            SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::ShadowUpdate);
            m_ShadowManager->UpdatePassUniforms();
        }

        {
            SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::GIProbeUpdate);
            m_ProbeManager->Update();
        }

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
        for (const auto& passName : m_ExecutionOrder)
        {
            auto pass = m_RenderPasses[passName];
            if (!pass->enabled)
                continue;
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
                        m_SkipFullscreenCommands = true;
                        m_RendererAPI->SetDebugView(state);
                        ExecuteRenderPass();
                        break;

                    default:
                        m_RendererAPI->SetDebugView(state);
                        ExecuteRenderPass();
                        break;
                }

                m_SkipFullscreenCommands = false;
                m_RendererAPI->SetDebugView(DebugViewState{});
            }
            else
            {
                ExecuteRenderPass();
            }

            EndRenderPass();
        }

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

        auto camera = Core::GetEngine().GetCameraManager()->GetActiveCamera();
        if(!camera)
            return false;

        out.view = camera->GetView();
        out.projection = camera->GetProjection();
        out.position = camera->parent->transform->GetWorldPosition();
        out.orthographic = camera->IsOrthographic();
        return true;
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
        // thumbnail rendered while no level is loaded).
        std::shared_ptr<Objects::Components::Camera> camera = nullptr;
        if(m_ViewStack.empty())
        {
            camera = Core::GetEngine().GetCameraManager()->GetActiveCamera();

            // Benign transient: right after boot (before the editor builds its viewport camera) or
            // during a level swap there can be a frame with no active camera. Skip the pass, and warn
            // only on the no-camera -> still-no-camera edge so a genuine "stuck" state stays visible
            // without spamming one line per pass per frame.
            static bool hadCameraLastCall = true;
            if(!camera)
            {
                if(hadCameraLastCall)
                    DEBUG_WARNING("No active camera - skipping render passes until one is set");
                hadCameraLastCall = false;
                return;
            }
            hadCameraLastCall = true;
        }

        // The camera's frustum only describes the active camera's view, so a pushed view is never culled.
        bool cullingActive = camera && camera->frustumCulling && m_CurrentPass->allowCulling;

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

                    if (worldMin != worldMax && !camera->IsInFrustum(worldMin, worldMax))
                        skip = true;
                }
            }

            if (skip || (m_SkipFullscreenCommands && drawCmd.fullscreenTri))
                continue;

            m_RendererAPI->ExecuteDrawCommand(drawCmd, m_CurrentPass);
        }
    }

    void Renderer::EndRenderPass()
    {
        if (m_CurrentPass->barrierAfter != MemoryBarrierBit::None)
            m_RendererAPI->MemoryBarrier(m_CurrentPass->barrierAfter);
    }
}
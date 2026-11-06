#pragma once

#include "engine/renderer/rhi/renderer_api.hpp"

#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"

#include "engine/renderer/rhi/pipelines/pipeline.hpp"

#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"

#include "engine/renderer/rhi/resources/mesh/mesh.hpp"

#include "engine/renderer/rhi/render_view.hpp"
#include "engine/renderer/rhi/render_pass.hpp"
#include "engine/renderer/rhi/graph/render_graph.hpp"
#include "engine/renderer/frontend/pipelines/render_pipeline.hpp"
#include "engine/renderer/frontend/render_feature.hpp"
#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/render_thread.hpp"
#include "engine/renderer/features/immediate/immediate_renderer.hpp"
#include "engine/renderer/features/immediate/thumbnail_service.hpp"
#include "engine/renderer/features/culling/occlusion_culling_manager.hpp"
#include "engine/renderer/features/billboards/billboard_renderer.hpp"
#include "engine/renderer/features/sprites/sprite_renderer.hpp"
#include "engine/assets/project/project.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <map>
#include <variant>

namespace Shard::Engine::Rendering {

    /// @todo move this in a "setting manager" system
    struct RendererSettings{
        int viewportWidth;
        int viewportHeight;
        bool multisampling = true;
        RendererAPI::API api;
    };

    class Renderer : public IRenderContext, public ISceneBinding{
        public:
            void Init(std::shared_ptr<RendererSettings> initialSettings);
            uint64_t GenerateSortKey(const DrawCommand &cmd, const uint32_t submeshID);
            std::shared_ptr<Pipeline> GetOrAddPipeline(const PipelineSpecifications &specs) override;
            std::shared_ptr<ComputePipeline> GetOrAddComputePipeline(const ComputePipelineSpecifications &specs) override;
            void DispatchCompute(const std::shared_ptr<ComputePipeline> pipeline, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ, MemoryBarrierBit barriersAfter = MemoryBarrierBit::None) override;
            void Render();

            RenderSnapshotBuffer& GetSnapshotBuffer() override { return m_SnapshotBuffer; }
            const RenderSnapshot& GetSnapshot() const override
            {
                return (RenderThread::Get().IsGraphicsThread() && m_FrameLease) ? m_FrameLease.Get() : m_SnapshotBuffer.Latest();
            }

            std::function<void(int, int)> onViewportResized;
            void Shutdown();
            void ClearPassesContent();
            // Draw lists belong to the render thread. Other threads compare this epoch to learn their commands were wiped.
            uint64_t GetContentEpoch() const { return m_ContentEpoch.load(std::memory_order_acquire); }
            void AddOrUpdateCommands(const std::vector<DrawCommand>& commands, const std::vector<std::string>& passes, bool addToShadowDrawList) override;
            void RemoveCommands(const std::vector<uint64_t> commandsID, const std::vector<std::string>& passes, bool removeFromShadowDrawList) override;

            void AddRenderPass(const std::shared_ptr<RenderPass> pass, const std::string& name, const std::vector<std::string>& dependencies) override;
            void RemoveRenderPass(const std::string& name) override;
            void AddDependencyToPass(const std::string& passName, const std::string& dependencyName) override;
            void RemoveDependencyFromPass(const std::string &passName, const std::string &dependencyName) override;

            std::shared_ptr<RenderPass> GetRenderPass(const std::string& name) const override
            {
                return OnGraphicsThread([&]() -> std::shared_ptr<RenderPass> {
                    auto it = m_RenderPasses.find(name);

                    if (it != m_RenderPasses.end())
                        return it->second;

                    return nullptr;
                });
            }

            std::vector<DrawCommand>* GetShadowDrawList() override { return &shadowDrawList; }

            bool HasRenderPass(const std::string& name) const override
            {
                return OnGraphicsThread([&] { return m_RenderPasses.find(name) != m_RenderPasses.end(); });
            }

            void RescaleFramebuffers(int newWidth, int newHeight);

            void PushView(const RenderView& view) override { m_ViewStack.push_back(view); }
            void PopView() override { if (!m_ViewStack.empty()) m_ViewStack.pop_back(); }
            bool HasViewOverride() const override { return !m_ViewStack.empty(); }

bool GetCurrentView(RenderView& out) override;

            void BindWorldState(RendererAPI& api, std::shared_ptr<Shader> shader, glm::mat4 modelMatrix, int objectID, bool applyPassGlobals) override;
            void BindMaterialScene(std::shared_ptr<Material> material) override;

            void RenderImmediate(const std::shared_ptr<RenderPass>& pass, const RenderView& view) override;

            void SetDebugView(const DebugViewState& state)
            {
                std::lock_guard<std::mutex> lock(m_DebugViewMutex);
                m_PendingDebugView = state;
            }
            DebugViewState GetDebugView() const
            {
                std::lock_guard<std::mutex> lock(m_DebugViewMutex);
                return m_PendingDebugView;
            }

            void SetLightingTerms(uint32_t terms) { m_LightingTerms.store(terms & LightingTermsAll, std::memory_order_relaxed); }
            uint32_t GetLightingTerms() const { return m_LightingTerms.load(std::memory_order_relaxed); }

            ImmediateRenderer* GetImmediateRenderer() override { return &m_ImmediateRenderer; }
            ThumbnailService* GetThumbnailService() override { return &m_ThumbnailService; }

            uint32_t GetViewportTextureHandle() const { return m_ViewportBuffer->GetResolveColorAttachment(); }

            std::shared_ptr<Framebuffer> GetViewportFramebuffer() const override { return m_ViewportBuffer; }

            std::shared_ptr<Framebuffer> GetSceneFramebuffer() const { return m_SceneBuffer; }
            glm::ivec2 GetViewportSize() const { return glm::ivec2(m_ViewportBuffer ? m_ViewportBuffer->GetWidth() : 0, m_ViewportBuffer ? m_ViewportBuffer->GetHeight() : 0); }

            void SetRenderPipeline(const std::string& name);
            std::string GetRenderPipelineName() const { std::lock_guard<std::mutex> lock(m_PipelineSelectMutex); return m_RequestedPipeline; }
            std::string GetActiveRenderPipelineName() const { std::lock_guard<std::mutex> lock(m_PipelineSelectMutex); return m_ActivePipelineName; }

            void AddRenderFeature(const std::shared_ptr<RenderFeature>& feature);
            void RemoveRenderFeature(const std::shared_ptr<RenderFeature>& feature);
            std::vector<std::string> GetRenderFeatureNames() const;
            void OnPipelineTypeRemoved(const std::string& type);

            void PresentToScreen(int screenWidth, int screenHeight) { m_ViewportBuffer->BlitToScreen(screenWidth, screenHeight); }

            void ToggleMultisampling(const bool on);

            void SetMultisampleCount(uint32_t samples);

            void SetShadowSettings(const Projects::ShadowSettings& settings);

            void SetOcclusionCullingEnabled(bool enabled) { m_OcclusionCullingEnabled = enabled; }
            bool IsOcclusionCullingEnabled() const { return m_OcclusionCullingEnabled; }
            OcclusionCullingManager* GetOcclusionCulling() const { return m_OcclusionCulling.get(); }

            void SetShowEditorBillboards(bool show) { m_BillboardRenderer.SetShowEditorBillboards(show); }
            bool GetShowEditorBillboards() const { return m_BillboardRenderer.GetShowEditorBillboards(); }
            uint32_t GetBillboardCount() const { return m_BillboardRenderer.GetDrawnCount(); }

            BillboardPick PickBillboard(float mouseX, float mouseY, float viewportWidth, float viewportHeight) const
            {
                return BillboardRenderer::Pick(GetSnapshot(), m_BillboardRenderer.GetShowEditorBillboards(), mouseX, mouseY, viewportWidth, viewportHeight);
            }

            std::string GetRenderGraphText() const { std::lock_guard<std::mutex> lock(m_GraphMutex); return m_GraphText; }
            std::string GetRenderGraphDot() const { std::lock_guard<std::mutex> lock(m_GraphMutex); return m_GraphDot; }

            std::string DumpFrame(const std::string& directory);

            bool WriteScreenshot(const std::string& path, std::string& error);

            std::string GetDeviceVendor();
            std::string GetRendererName();
            std::string GetDriverVersion();
            uint32_t GetPassesCount() const { return m_PublishedPassCount; }
            uint32_t GetDrawCallsCount() const { return m_DrawCallsCount; }
            uint32_t GetPrimitivesCount() const { return m_PrimitivesCount; }
            uint32_t GetVerticesCount() const { return m_VerticesCount; }
            
            RendererAPI* GetRendererAPI() const override
            {
                if(m_RendererAPI) 
                    return m_RendererAPI.get();
                else
                    return nullptr; 
            }
            const std::shared_ptr<Mesh> GetUnitCube() override { return m_UnitCube; }
            const std::shared_ptr<Mesh> GetUnitQuad() override { return m_UnitQuad; }
            const std::shared_ptr<ShadowManager> GetShadowManager() override { return m_ShadowManager; }
            const std::shared_ptr<LightManager> GetLightManager() override { return m_LightManager; }
            const std::shared_ptr<ProbeManager> GetProbeManager() override { return m_ProbeManager; }
            const std::shared_ptr<LightmapManager> GetLightmapManager() override { return m_LightmapManager; }
            const std::shared_ptr<SSAOManager> GetSSAOManager() override { return m_SSAOManager; }
            const std::shared_ptr<LightCullingManager> GetLightCullingManager() override { return m_LightCullingManager; }
            const std::shared_ptr<SSRManager>& GetSSRManager() const { return m_SSRManager; }

        private:

            void BeginFrame();

            void SyncSceneDrawLists();
            void SyncSkyboxDrawCommand();
            void DrawFrame();
            void EndFrame();

            void ReorderDrawList();

            void BuildExecutionOrder();
            uint64_t ComputeGraphSignature() const;
            std::string GraphResourceName(const std::shared_ptr<Framebuffer>& target) const;

            void BeginRenderPass(const std::shared_ptr<RenderPass>& pass);
            void ExecuteRenderPass();
            void EndRenderPass();

            bool PrepareOcclusionCulling(const std::string& passName, const std::shared_ptr<RenderPass>& pass);
            void ExecuteOcclusionCulledPass(const std::string& passName, const std::shared_ptr<RenderPass>& pass);
            OcclusionCullingManager::Target MakeOcclusionTarget(const std::shared_ptr<RenderPass>& pass) const;

            std::shared_ptr<RendererAPI> m_RendererAPI;

            RenderSnapshotBuffer m_SnapshotBuffer;
            RenderSnapshotBuffer::ReadLease m_FrameLease;

            std::unordered_map<std::string, std::shared_ptr<RenderPass>> m_RenderPasses;
            std::unique_ptr<RenderPipeline> m_ActivePipeline;
            std::vector<std::shared_ptr<RenderFeature>> m_Features;
            mutable std::mutex m_FeaturesMutex;
            uint32_t m_FeatureListener = 0;
            uint32_t m_PipelineListener = 0;
            void ConnectPluginExtensions();
            void DisconnectPluginExtensions();
            mutable std::mutex m_PipelineSelectMutex;
            std::string m_RequestedPipeline = "forward+";
            std::string m_ActivePipelineName;
            void ActivatePipeline(const std::string& name);
            void ApplyPipelineForFrame();

            std::vector<std::string> m_ExecutionOrder;
            std::vector<MemoryBarrierBit> m_ExecutionBarriers;
            RenderGraph m_Graph;
            CompiledGraph m_CompiledGraph;
            uint64_t m_GraphSignature = 0;
            mutable std::mutex m_GraphMutex;
            std::string m_GraphText;
            std::string m_GraphDot;
            std::shared_ptr<RenderPass> m_CurrentPass;
            std::vector<RenderView> m_ViewStack;
            std::vector<const DrawCommand*> m_VisibleCommands;
            ImmediateRenderer m_ImmediateRenderer;
            ThumbnailService m_ThumbnailService;
            std::vector<std::string> m_PassInsertionOrder;

            struct SyncedMesh {
                std::shared_ptr<Mesh> mesh;
                std::vector<std::shared_ptr<Material>> materials;
                glm::mat4 worldMatrix = glm::mat4(1.0f);
                std::vector<std::string> extraPasses;
                int objectId = -1;
                int componentId = -1;
                uint64_t lightmapHandle = 0;
                glm::vec4 lightmapScaleOffset = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
                uint64_t seenInSync = 0;
            };
            std::unordered_map<uint64_t, SyncedMesh> m_SyncedMeshes;
            uint64_t m_SyncCount = 0;
            std::shared_ptr<Material> m_SyncedSkyMaterial;
            int m_SyncedSkyObjectId = -1;
            void RemoveSyncedMeshCommands(const SyncedMesh& synced);
            
            std::unordered_map<std::string, std::vector<std::string>> m_RenderPassDependencies;
            std::unordered_map<std::string, std::vector<std::string>> m_RenderPassDependents;

            std::shared_ptr<Mesh> m_UnitCube = nullptr;
            std::shared_ptr<Mesh> m_UnitQuad = nullptr;

            std::shared_ptr<Framebuffer> m_ViewportBuffer;

            // 16-bit float, linear, unclamped. A lit pixel is far above 1 and an 8-bit target would clip it before tonemapping.
            std::shared_ptr<Framebuffer> m_SceneBuffer;

            std::shared_ptr<Shader> m_TonemapShader;
            std::shared_ptr<Material> m_TonemapMaterial;
            std::shared_ptr<RenderPass> m_TonemapPass;
            void EnsureTonemapPass();

            void DrawPluginOverlays(bool intoTextures);
            std::shared_ptr<RenderPass> m_OverlayPass;

            void ResolveSceneToViewport();

            int GetDisplayMode() const;

            std::shared_ptr<ShadowManager> m_ShadowManager;
            std::vector<DrawCommand> shadowDrawList = {};
            std::unordered_map<uint64_t, size_t> shadowDrawCommandsLookup;
            std::shared_ptr<LightManager> m_LightManager;
            std::shared_ptr<ProbeManager> m_ProbeManager;
            std::shared_ptr<LightmapManager> m_LightmapManager;
            std::shared_ptr<SSAOManager> m_SSAOManager;
            std::shared_ptr<SSRManager> m_SSRManager;
            std::shared_ptr<LightCullingManager> m_LightCullingManager;
            BillboardRenderer m_BillboardRenderer;
            SpriteRenderer m_SpriteRenderer;
            std::shared_ptr<OcclusionCullingManager> m_OcclusionCulling;
            std::atomic<bool> m_OcclusionCullingEnabled{true};
            int m_OcclusionPhase = 0;

            std::mutex m_PipelineMutex;
            std::unordered_map<PipelineSpecifications,std::shared_ptr<Pipeline>,PipelineSpecsHash> m_Pipelines;
            std::unordered_map<ComputePipelineSpecifications,std::shared_ptr<ComputePipeline>,ComputePipelineSpecsHash> m_ComputePipelines;

            bool m_MultisamplingEnabled = false;

            std::shared_ptr<RendererSettings> m_Settings;

            bool m_NeedExecutionOrderRebuild = false;

            DebugViewState m_DebugView;
            DebugViewState m_PendingDebugView;
            mutable std::mutex m_DebugViewMutex;
            std::atomic<uint32_t> m_LightingTerms{LightingTermsAll};
            bool m_SkipFullscreenCommands = false;

            std::atomic<uint32_t> m_PublishedPassCount{0};
            std::atomic<int> m_PublishedLightCount{0};

            std::atomic<uint32_t> m_DrawCallsCount{0};
            std::atomic<uint32_t> m_PrimitivesCount{0};
            std::atomic<uint32_t> m_VerticesCount{0};
            std::unordered_map<uint64_t, uint32_t> m_CommandRefCount;
            std::atomic<uint64_t> m_ContentEpoch{ 0 };
    };
}
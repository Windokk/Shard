#pragma once

#include "engine/renderer/rhi/renderer_api.hpp"

#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"

#include "engine/renderer/rhi/pipelines/pipeline.hpp"

#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"

#include "engine/renderer/rhi/resources/mesh/mesh.hpp"

#include "engine/renderer/rhi/render_view.hpp"
#include "engine/renderer/rhi/render_pass.hpp"
#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/render_thread.hpp"
#include "engine/renderer/features/immediate/immediate_renderer.hpp"
#include "engine/renderer/features/immediate/thumbnail_service.hpp"
#include "engine/renderer/features/culling/occlusion_culling_manager.hpp"
#include "engine/renderer/features/billboards/billboard_renderer.hpp"

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
            /// The frame being drawn on the graphics thread (the one it holds a lease on), the newest published one anywhere else
            /// (the simulation, which is also the thread that publishes).
            const RenderSnapshot& GetSnapshot() const override
            {
                return (RenderThread::Get().IsGraphicsThread() && m_FrameLease) ? m_FrameLease.Get() : m_SnapshotBuffer.Latest();
            }

            /// Called when the viewport is resized, once the framebuffers follow : whoever owns the cameras resizes them
            /// (the renderer itself knows no camera).
            std::function<void(int, int)> onViewportResized;
            void Shutdown();
            void ClearPassesContent();
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

            /// @brief Overrides the view every draw is issued from, until the matching PopView().
            /// Nests. While a view is pushed, frustum culling is skipped (the camera's frustum belongs
            /// to the active camera, not to the pushed view).
            void PushView(const RenderView& view) override { m_ViewStack.push_back(view); }
            void PopView() override { if (!m_ViewStack.empty()) m_ViewStack.pop_back(); }
            bool HasViewOverride() const override { return !m_ViewStack.empty(); }

            /// @brief The view draws are currently issued from : the top pushed view, else the active
            /// camera's. Returns false when there is neither.
bool GetCurrentView(RenderView& out) override;

            // ISceneBinding (frontend/scene_binding.cpp)
            void BindWorldState(RendererAPI& api, std::shared_ptr<Shader> shader, glm::mat4 modelMatrix, int objectID, bool applyPassGlobals) override;
            void BindMaterialScene(std::shared_ptr<Material> material) override;

            /// @brief Runs one pass right now from `view`, outside the retained pass graph (see
            /// ImmediateRenderer, which is the intended caller). The pass is not registered anywhere.
            void RenderImmediate(const std::shared_ptr<RenderPass>& pass, const RenderView& view) override;

            /// @brief Editor viewport debug view for the main scene render (view mode, lighting/shadow toggles).
            /// Takes effect at the start of the next frame
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

            ImmediateRenderer* GetImmediateRenderer() override { return &m_ImmediateRenderer; }
            ThumbnailService* GetThumbnailService() override { return &m_ThumbnailService; }

            /// The picture : what the UI shows, the screenshots read and the window presents (8 bit, display colours). The
            /// scene is rendered in HDR into another target and mapped into this one at the end of the forward pass, the
            /// overlays are drawn on it afterwards (see ResolveSceneToViewport).
            uint32_t GetViewportTextureHandle() const { return m_ViewportBuffer->GetResolveColorAttachment(); }

            std::shared_ptr<Framebuffer> GetViewportFramebuffer() const override { return m_ViewportBuffer; }

            void PresentToScreen(int screenWidth, int screenHeight) { m_ViewportBuffer->BlitToScreen(screenWidth, screenHeight); }

            void ToggleMultisampling(const bool on);

            /// @brief Samples per pixel of the viewport's multisampled targets (the GPU's maximum caps it).
            void SetMultisampleCount(uint32_t samples);

            /// @brief GPU occlusion culling of the main camera's geometry passes (see OcclusionCullingManager).
            /// On by default; it only ever skips geometry that is hidden, so toggling it never changes the image.
            void SetOcclusionCullingEnabled(bool enabled) { m_OcclusionCullingEnabled = enabled; }
            bool IsOcclusionCullingEnabled() const { return m_OcclusionCullingEnabled; }
            OcclusionCullingManager* GetOcclusionCulling() const { return m_OcclusionCulling.get(); }

            /// @brief The billboards flagged EditorOnly (light, camera and probe volume icons...) are drawn only while this is on : the editor turns it on.
            void SetShowEditorBillboards(bool show) { m_BillboardRenderer.SetShowEditorBillboards(show); }
            bool GetShowEditorBillboards() const { return m_BillboardRenderer.GetShowEditorBillboards(); }
            uint32_t GetBillboardCount() const { return m_BillboardRenderer.GetDrawnCount(); }

            /// @brief The billboard under a pixel of the viewport (from its top left) : what a click on an icon selects.
            BillboardPick PickBillboard(float mouseX, float mouseY, float viewportWidth, float viewportHeight) const
            {
                return BillboardRenderer::Pick(GetSnapshot(), m_BillboardRenderer.GetShowEditorBillboards(), mouseX, mouseY, viewportWidth, viewportHeight);
            }

            std::string GetDeviceVendor();
            std::string GetRendererName();
            std::string GetDriverVersion();
            const uint32_t GetPassesCount() const { return m_PublishedPassCount; }
            const uint32_t GetDrawCallsCount() const { return m_DrawCallsCount; }
            const uint32_t GetPrimitivesCount() const { return m_PrimitivesCount; }
            const uint32_t GetVerticesCount() const { return m_VerticesCount; }
            
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

        private:

            void BeginFrame();

            /// Brings what the renderer keeps (draw lists, lights, GI probe volumes) in line with the frame's snapshot
            void SyncSceneDrawLists();
            void SyncSkyboxDrawCommand();
            void DrawFrame();
            void EndFrame();

            void ReorderDrawList();
            void BuildExecutionOrder();

            void BeginRenderPass(const std::shared_ptr<RenderPass>& pass);
            void ExecuteRenderPass();
            void EndRenderPass();

            // GPU occlusion culling. Prepare... runs before the pass starts (it records phase 1 on the GPU) and says
            // whether this pass is culled this frame; ExecuteOcclusionCulledPass then replaces ExecuteRenderPass()
            // with the two-phase draw (see OcclusionCullingManager).
            bool PrepareOcclusionCulling(const std::string& passName, const std::shared_ptr<RenderPass>& pass);
            void ExecuteOcclusionCulledPass(const std::string& passName, const std::shared_ptr<RenderPass>& pass);
            OcclusionCullingManager::Target MakeOcclusionTarget(const std::shared_ptr<RenderPass>& pass) const;

            std::shared_ptr<RendererAPI> m_RendererAPI;

            // The simulation's frame : filled by the Extract stage, read through the lease while a frame renders
            RenderSnapshotBuffer m_SnapshotBuffer;
            RenderSnapshotBuffer::ReadLease m_FrameLease;

            std::unordered_map<std::string, std::shared_ptr<RenderPass>> m_RenderPasses;
            std::vector<std::string> m_ExecutionOrder;
            std::shared_ptr<RenderPass> m_CurrentPass;
            std::vector<RenderView> m_ViewStack;
            std::vector<const DrawCommand*> m_VisibleCommands;      // what ExecuteRenderPass() drew : culled once, drawn per sweep
            ImmediateRenderer m_ImmediateRenderer;
            ThumbnailService m_ThumbnailService;
            std::vector<std::string> m_PassInsertionOrder;

            // What SyncSceneDrawLists() last submitted for each mesh proxy, to submit only the difference
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

            std::shared_ptr<Framebuffer> m_ViewportBuffer;      // the display target : 8 bit, display colours

            // The forward pass draws into this one : 16 bit floats, linear light, no clamping. Lighting is arithmetic on physical
            // quantities and a lit pixel is far brighter than 1 as soon as there is a sun or a lamp, which an 8 bit target
            // would clip before anything could roll it off.
            std::shared_ptr<Framebuffer> m_SceneBuffer;

            // The pass that turns the HDR scene into the viewport (tonemap.frag) : built the first time it is needed
            std::shared_ptr<Shader> m_TonemapShader;
            std::shared_ptr<Material> m_TonemapMaterial;
            std::shared_ptr<RenderPass> m_TonemapPass;
            void EnsureTonemapPass();

            /// End of the forward pass : resolves the HDR scene, gives the viewport its depth (what the overlays are tested
            /// against) and maps the light of the scene onto it - exposure, tonemapper, sRGB curve.
            void ResolveSceneToViewport();

            /// How the picture is shown for the debug view that is on (see tonemap.frag's displayMode)
            int GetDisplayMode() const;

            std::shared_ptr<ShadowManager> m_ShadowManager;
            std::vector<DrawCommand> shadowDrawList = {};
            std::unordered_map<uint64_t, size_t> shadowDrawCommandsLookup;
            std::shared_ptr<LightManager> m_LightManager;
            std::shared_ptr<ProbeManager> m_ProbeManager;
            std::shared_ptr<LightmapManager> m_LightmapManager;
            std::shared_ptr<SSAOManager> m_SSAOManager;
            std::shared_ptr<LightCullingManager> m_LightCullingManager;
            BillboardRenderer m_BillboardRenderer;
            std::shared_ptr<OcclusionCullingManager> m_OcclusionCulling;
            std::atomic<bool> m_OcclusionCullingEnabled{true};
            // 0 = not occlusion culling, 1/2 = which phase ExecuteRenderPass is currently issuing draws for
            int m_OcclusionPhase = 0;

            std::mutex m_PipelineMutex;     // pipelines are asked for by the simulation (components) and by the renderer
            std::unordered_map<PipelineSpecifications,std::shared_ptr<Pipeline>,PipelineSpecsHash> m_Pipelines;
            std::unordered_map<ComputePipelineSpecifications,std::shared_ptr<ComputePipeline>,ComputePipelineSpecsHash> m_ComputePipelines;

            bool m_MultisamplingEnabled = false;

            std::shared_ptr<RendererSettings> m_Settings;

            bool m_NeedExecutionOrderRebuild = false;

            DebugViewState m_DebugView;             // what the frame being drawn uses (see SetDebugView)
            DebugViewState m_PendingDebugView;
            mutable std::mutex m_DebugViewMutex;
            bool m_SkipFullscreenCommands = false;

            // The sizes of what only the render thread may look into, as of the last frame (for the statistics of the simulation)
            std::atomic<uint32_t> m_PublishedPassCount{0};
            std::atomic<int> m_PublishedLightCount{0};

            // Read by the profiler statistics from another thread than the one that draws
            std::atomic<uint32_t> m_DrawCallsCount{0};
            std::atomic<uint32_t> m_PrimitivesCount{0};
            std::atomic<uint32_t> m_VerticesCount{0};
            std::unordered_map<uint64_t, uint32_t> m_CommandRefCount;
    };
}
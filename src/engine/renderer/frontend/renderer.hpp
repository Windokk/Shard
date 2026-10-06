#pragma once

#include "engine/renderer/rhi/renderer_api.hpp"

#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"

#include "engine/renderer/rhi/pipelines/pipeline.hpp"

#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"

#include "engine/renderer/rhi/resources/mesh/mesh.hpp"

#include "engine/renderer/rhi/render_view.hpp"
#include "engine/renderer/rhi/render_pass.hpp"
#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/features/immediate/immediate_renderer.hpp"
#include "engine/renderer/features/immediate/thumbnail_service.hpp"

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
                auto it = m_RenderPasses.find(name);

                if (it != m_RenderPasses.end())
                    return it->second;

                return nullptr;
            }

            std::vector<DrawCommand>* GetShadowDrawList() override { return &shadowDrawList; }

            bool HasRenderPass(const std::string& name) const override { return m_RenderPasses.find(name) != m_RenderPasses.end(); }

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
            void SetDebugView(const DebugViewState& state) { m_DebugView = state; }
            const DebugViewState& GetDebugView() const { return m_DebugView; }

            ImmediateRenderer* GetImmediateRenderer() override { return &m_ImmediateRenderer; }
            ThumbnailService* GetThumbnailService() override { return &m_ThumbnailService; }

            uint32_t GetViewportTextureHandle() const { return m_ViewportBuffer->GetResolveColorAttachment(); }

            std::shared_ptr<Framebuffer> GetViewportFramebuffer() const override { return m_ViewportBuffer; }

            void PresentToScreen(int screenWidth, int screenHeight) { m_ViewportBuffer->BlitToScreen(screenWidth, screenHeight); }

            void ToggleMultisampling(const bool on);

            std::string GetDeviceVendor();
            std::string GetRendererName();
            std::string GetDriverVersion();
            const uint32_t GetPassesCount() const { return m_RenderPasses.size(); }
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
            const std::shared_ptr<SSAOManager> GetSSAOManager() override { return m_SSAOManager; }
            const std::shared_ptr<LightCullingManager> GetLightCullingManager() override { return m_LightCullingManager; }
            const std::shared_ptr<Material> GetDebugMaterial() override { return m_DebugMat; }

        private:

            void BeginFrame();
            void DrawFrame();
            void EndFrame();

            void ReorderDrawList();
            void BuildExecutionOrder();

            void BeginRenderPass(const std::shared_ptr<RenderPass>& pass);
            void ExecuteRenderPass();
            void EndRenderPass();

            std::shared_ptr<RendererAPI> m_RendererAPI;

            std::unordered_map<std::string, std::shared_ptr<RenderPass>> m_RenderPasses;
            std::vector<std::string> m_ExecutionOrder;
            std::shared_ptr<RenderPass> m_CurrentPass;
            std::vector<RenderView> m_ViewStack;
            ImmediateRenderer m_ImmediateRenderer;
            ThumbnailService m_ThumbnailService;
            std::vector<std::string> m_PassInsertionOrder;
            
            std::unordered_map<std::string, std::vector<std::string>> m_RenderPassDependencies;
            std::unordered_map<std::string, std::vector<std::string>> m_RenderPassDependents;

            std::shared_ptr<Mesh> m_UnitCube = nullptr;
            std::shared_ptr<Mesh> m_UnitQuad = nullptr;

            std::shared_ptr<Framebuffer> m_ViewportBuffer;

            std::shared_ptr<ShadowManager> m_ShadowManager;
            std::vector<DrawCommand> shadowDrawList = {};
            std::unordered_map<uint64_t, size_t> shadowDrawCommandsLookup;
            std::shared_ptr<LightManager> m_LightManager;
            std::shared_ptr<ProbeManager> m_ProbeManager;
            std::shared_ptr<SSAOManager> m_SSAOManager;
            std::shared_ptr<LightCullingManager> m_LightCullingManager;

            std::unordered_map<PipelineSpecifications,std::shared_ptr<Pipeline>,PipelineSpecsHash> m_Pipelines;
            std::unordered_map<ComputePipelineSpecifications,std::shared_ptr<ComputePipeline>,ComputePipelineSpecsHash> m_ComputePipelines;

            bool m_MultisamplingEnabled = false;

            std::shared_ptr<RendererSettings> m_Settings;

            bool m_NeedExecutionOrderRebuild = false;

            DebugViewState m_DebugView;
            bool m_SkipFullscreenCommands = false;

            uint32_t m_DrawCallsCount = 0;
            uint32_t m_PrimitivesCount = 0;
            uint32_t m_VerticesCount = 0;
            std::unordered_map<uint64_t, uint32_t> m_CommandRefCount;

            std::shared_ptr<Material> m_DebugMat = nullptr;
    };
}
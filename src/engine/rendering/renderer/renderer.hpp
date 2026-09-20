#pragma once

#include "engine/rendering/renderer/renderer_api.hpp"

#include "engine/rendering/framebuffer/framebuffer.hpp"

#include "engine/rendering/pipeline/pipeline.hpp"

#include "engine/rendering/pipeline/compute_pipeline.hpp"

#include "engine/rendering/mesh/mesh.hpp"

#include "engine/rendering/renderer/render_view.hpp"
#include "engine/rendering/immediate/immediate_renderer.hpp"
#include "engine/rendering/immediate/thumbnail_service.hpp"

#include <map>
#include <variant>

namespace Shard::Engine::Rendering {

    class LightManager;
    class ShadowManager;
    class ProbeManager;
    class SSAOManager;
    class LightCullingManager;
    using NumericValue = std::variant<bool, float, int, glm::vec2, glm::vec3, glm::vec4, glm::mat4>;

    class Renderer;

    struct RenderPass{
        std::shared_ptr<Framebuffer> target = nullptr;
        bool clearColor = true;
        bool clearDepth = true;
        std::map<std::string, NumericValue> customUniforms;
        std::map<std::string, uint64_t> customSamplers;
        bool overridePipeline = false;
        std::shared_ptr<Pipeline> customPipeline = nullptr;
        bool allowResize = true;
        bool allowCulling = true;
        // Skipped entirely in Renderer::DrawFrame() when false - lets a pass stay registered (keeping
        // its dependents, target, draw list, etc. intact) while producing nothing this frame, e.g. the
        // editor hiding probe-marker gizmos (ProbeGizmoPass) without touching the ProbeVolume components
        // that actually drive GI (which stay active either way).
        bool enabled = true;
        // Issued once, right after this pass finishes executing (Renderer::EndRenderPass) - needed only
        // when something this pass wrote is later read in a way the driver can't track through normal
        // bind-point synchronization (e.g. a bindless texture handle sampled by a later pass, mirroring
        // why DispatchCompute takes the same MemoryBarrierBit for image-load-store writes read by a
        // subsequent draw). None for every ordinary pass.
        MemoryBarrierBit barrierAfter = MemoryBarrierBit::None;
        std::vector<DrawCommand> drawList = {};
        std::unordered_map<uint64_t, size_t> drawCommandsLookup;
        std::vector<DrawCommand>* externalDrawList = nullptr;
    };


    /// @todo move this in a "setting manager" system
    struct RendererSettings{
        int viewportWidth;
        int viewportHeight;
        bool multisampling = true;
        RendererAPI::API api;
    };

    class Renderer{
        public:
            void Init(std::shared_ptr<RendererSettings> initialSettings);
            uint64_t GenerateSortKey(const DrawCommand &cmd, const uint32_t submeshID);
            std::shared_ptr<Pipeline> GetOrAddPipeline(const PipelineSpecifications &specs);
            std::shared_ptr<ComputePipeline> GetOrAddComputePipeline(const ComputePipelineSpecifications &specs);
            void DispatchCompute(const std::shared_ptr<ComputePipeline> pipeline, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ, MemoryBarrierBit barriersAfter = MemoryBarrierBit::None);
            void Render();
            void Shutdown();
            void ClearPassesContent();
            void AddOrUpdateCommands(const std::vector<DrawCommand>& commands, const std::vector<std::string>& passes, bool addToShadowDrawList);
            void RemoveCommands(const std::vector<uint64_t> commandsID, const std::vector<std::string>& passes, bool removeFromShadowDrawList);

            void AddRenderPass(const std::shared_ptr<RenderPass> pass, const std::string& name, const std::vector<std::string>& dependencies);
            void RemoveRenderPass(const std::string& name);
            void AddDependencyToPass(const std::string& passName, const std::string& dependencyName);
            void RemoveDependencyFromPass(const std::string &passName, const std::string &dependencyName);

            std::shared_ptr<RenderPass> GetRenderPass(const std::string& name) const
            {
                auto it = m_RenderPasses.find(name);

                if (it != m_RenderPasses.end())
                    return it->second;

                return nullptr;
            }

            std::vector<DrawCommand>* GetShadowDrawList(){ return &shadowDrawList; }

            bool HasRenderPass(const std::string& name) const { return m_RenderPasses.find(name) != m_RenderPasses.end(); }

            void RescaleFramebuffers(int newWidth, int newHeight);

            /// @brief Overrides the view every draw is issued from, until the matching PopView().
            /// Nests. While a view is pushed, frustum culling is skipped (the camera's frustum belongs
            /// to the active camera, not to the pushed view).
            void PushView(const RenderView& view) { m_ViewStack.push_back(view); }
            void PopView() { if (!m_ViewStack.empty()) m_ViewStack.pop_back(); }
            bool HasViewOverride() const { return !m_ViewStack.empty(); }

            /// @brief The view draws are currently issued from : the top pushed view, else the active
            /// camera's. Returns false when there is neither.
            bool GetCurrentView(RenderView& out);

            /// @brief Runs one pass right now from `view`, outside the retained pass graph (see
            /// ImmediateRenderer, which is the intended caller). The pass is not registered anywhere.
            void RenderImmediate(const std::shared_ptr<RenderPass>& pass, const RenderView& view);

            /// @brief Editor viewport debug view for the main scene render (view mode, lighting/shadow toggles).
            void SetDebugView(const DebugViewState& state) { m_DebugView = state; }
            const DebugViewState& GetDebugView() const { return m_DebugView; }

            ImmediateRenderer* GetImmediateRenderer() { return &m_ImmediateRenderer; }
            ThumbnailService* GetThumbnailService() { return &m_ThumbnailService; }

            uint32_t GetViewportTextureHandle() const { return m_ViewportBuffer->GetResolveColorAttachment(); }

            std::shared_ptr<Framebuffer> GetViewportFramebuffer() const { return m_ViewportBuffer; }

            void PresentToScreen(int screenWidth, int screenHeight) { m_ViewportBuffer->BlitToScreen(screenWidth, screenHeight); }

            void ToggleMultisampling(const bool on);

            std::string GetDeviceVendor();
            std::string GetRendererName();
            std::string GetDriverVersion();
            const uint32_t GetPassesCount() const { return m_RenderPasses.size(); }
            const uint32_t GetDrawCallsCount() const { return m_DrawCallsCount; }
            const uint32_t GetPrimitivesCount() const { return m_PrimitivesCount; }
            const uint32_t GetVerticesCount() const { return m_VerticesCount; }
            
            RendererAPI* GetRendererAPI() const 
            {
                if(m_RendererAPI) 
                    return m_RendererAPI.get();
                else
                    return nullptr; 
            }
            const std::shared_ptr<Mesh> GetUnitCube() { return m_UnitCube; }
            const std::shared_ptr<Mesh> GetUnitQuad() { return m_UnitQuad; }
            const std::shared_ptr<ShadowManager> GetShadowManager() { return m_ShadowManager; }
            const std::shared_ptr<LightManager> GetLightManager() { return m_LightManager; }
            const std::shared_ptr<ProbeManager> GetProbeManager() { return m_ProbeManager; }
            const std::shared_ptr<SSAOManager> GetSSAOManager() { return m_SSAOManager; }
            const std::shared_ptr<LightCullingManager> GetLightCullingManager() { return m_LightCullingManager; }
            const std::shared_ptr<Material> GetDebugMaterial() { return m_DebugMat; }

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
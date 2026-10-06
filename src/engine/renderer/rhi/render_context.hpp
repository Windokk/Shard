#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/renderer/rhi/render_pass.hpp"
#include "engine/renderer/rhi/render_view.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"

namespace Shard::Engine::Rendering {

    class Framebuffer;
    class Mesh;
    class Material;
    class LightManager;
    class ShadowManager;
    class ProbeManager;
    class SSAOManager;
    class LightCullingManager;
    class ImmediateRenderer;
    class ThumbnailService;

    /// @brief What the parts of the renderer that draw or light the world (the features and the components)
    /// ask of the Renderer that drives the frame : pipelines, passes, draw commands and the other features.
    /// They only see this interface, the Renderer implements it.
    class IRenderContext{
        public:
            virtual ~IRenderContext() = default;

            virtual std::shared_ptr<Pipeline> GetOrAddPipeline(const PipelineSpecifications &specs) = 0;
            virtual std::shared_ptr<ComputePipeline> GetOrAddComputePipeline(const ComputePipelineSpecifications &specs) = 0;
            virtual void DispatchCompute(const std::shared_ptr<ComputePipeline> pipeline, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ, MemoryBarrierBit barriersAfter = MemoryBarrierBit::None) = 0;

            virtual void AddOrUpdateCommands(const std::vector<DrawCommand>& commands, const std::vector<std::string>& passes, bool addToShadowDrawList) = 0;
            virtual void RemoveCommands(const std::vector<uint64_t> commandsID, const std::vector<std::string>& passes, bool removeFromShadowDrawList) = 0;

            virtual void AddRenderPass(const std::shared_ptr<RenderPass> pass, const std::string& name, const std::vector<std::string>& dependencies) = 0;
            virtual void RemoveRenderPass(const std::string& name) = 0;
            virtual void AddDependencyToPass(const std::string& passName, const std::string& dependencyName) = 0;
            virtual void RemoveDependencyFromPass(const std::string &passName, const std::string &dependencyName) = 0;
            virtual std::shared_ptr<RenderPass> GetRenderPass(const std::string& name) const = 0;
            virtual bool HasRenderPass(const std::string& name) const = 0;
            virtual std::vector<DrawCommand>* GetShadowDrawList() = 0;

            virtual void PushView(const RenderView& view) = 0;
            virtual void PopView() = 0;
            virtual bool HasViewOverride() const = 0;
            virtual bool GetCurrentView(RenderView& out) = 0;
            virtual void RenderImmediate(const std::shared_ptr<RenderPass>& pass, const RenderView& view) = 0;

            virtual std::shared_ptr<Framebuffer> GetViewportFramebuffer() const = 0;
            virtual RendererAPI* GetRendererAPI() const = 0;

            virtual const std::shared_ptr<Mesh> GetUnitCube() = 0;
            virtual const std::shared_ptr<Mesh> GetUnitQuad() = 0;
            virtual const std::shared_ptr<Material> GetDebugMaterial() = 0;

            virtual const std::shared_ptr<ShadowManager> GetShadowManager() = 0;
            virtual const std::shared_ptr<LightManager> GetLightManager() = 0;
            virtual const std::shared_ptr<ProbeManager> GetProbeManager() = 0;
            virtual const std::shared_ptr<SSAOManager> GetSSAOManager() = 0;
            virtual const std::shared_ptr<LightCullingManager> GetLightCullingManager() = 0;
            virtual ImmediateRenderer* GetImmediateRenderer() = 0;
            virtual ThumbnailService* GetThumbnailService() = 0;
    };
}

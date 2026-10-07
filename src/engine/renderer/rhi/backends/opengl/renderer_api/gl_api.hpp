#pragma once

#include "engine/renderer/rhi/renderer_api.hpp"

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering{

    class Shader;

    class GLRendererAPI : public RendererAPI{
        
        public :
            void ExecuteDrawCommand(const DrawCommand& command, const std::shared_ptr<RenderPass> pass) override;

            void ExecuteComputeDispatch(const std::shared_ptr<ComputePipeline> pipeline, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) override;

            void MemoryBarrier(MemoryBarrierBit barriers) override;

            const ComputeLimits& GetComputeLimits() override;

            void ToggleMultisampling(const bool on) override;

            void BindTextureUnit(uint32_t binding, uint32_t handle) override;

            void SetViewport(uint32_t x, uint32_t y,
                                        uint32_t width, uint32_t height) override;

            void SetClearColor(float r, float g, float b, float a) override;

            glm::vec4 GetClearColor() override;

            void Clear(ClearBit clearBits) override;

            void SetIndirectDrawBuffer(uint32_t bufferHandle) override;

            void InvalidateStateCache() override;
            void SetDebugView(const DebugViewState& state) override;

            std::string GetDeviceVendor() override;
            std::string GetRendererName() override;
            std::string GetDriverVersion() override;

        private:

            ComputeLimits m_ComputeLimits;
            bool m_ComputeLimitsQueried = false;

            // What the previous ExecuteDrawCommand left bound. A mesh split into spatial chunks issues many
            // consecutive draws with the same pass, pipeline and material; for those only the per-object
            // uniforms can differ, so the (string-keyed, hence slow) material / scene / pass uniform uploads
            // are skipped. Forgotten whenever the GL state cache is (a pass start, a view-mode sweep).
            struct LastDraw {
                const RenderPass* pass = nullptr;
                const Pipeline* pipeline = nullptr;
                const Material* material = nullptr; // null for a pass that overrides the pipeline
                glm::mat4 modelMatrix = glm::mat4(1.0f);
                uint32_t objectID = 0;
            } m_LastDraw;

            void BindMesh(std::shared_ptr<Mesh> mesh);

            void BindPipeline(std::shared_ptr<Pipeline> pipeline);

            void BindMaterial(std::shared_ptr<Material> mat);

            void BindPassData(const std::shared_ptr<RenderPass> pass, std::shared_ptr<Pipeline> pipeline);

            void BindPassData(const std::shared_ptr<RenderPass> pass, std::shared_ptr<Material> material);

            void DrawIndexed(const std::shared_ptr<Pipeline> pipeline, uint32_t indexCount, uint32_t indexOffset);

            void DrawIndexedIndirect(const std::shared_ptr<Pipeline> pipeline, uint32_t slot);

            uint32_t m_IndirectBuffer = 0;
                
            void DrawFullScreenTriangle();
    };
}
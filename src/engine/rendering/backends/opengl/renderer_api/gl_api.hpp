#pragma once

#include "engine/rendering/renderer/renderer_api.hpp"

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

            void SetViewport(uint32_t x, uint32_t y,
                                        uint32_t width, uint32_t height) override;

            void SetClearColor(float r, float g, float b, float a) override;

            glm::vec4 GetClearColor() override;

            void Clear(ClearBit clearBits) override;

            void InvalidateStateCache() override;
            void SetDebugView(const DebugViewState& state) override;

            std::string GetDeviceVendor() override;
            std::string GetRendererName() override;
            std::string GetDriverVersion() override;

        private:

            ComputeLimits m_ComputeLimits;
            bool m_ComputeLimitsQueried = false;

            void BindMesh(std::shared_ptr<Mesh> mesh);

            void BindPipeline(std::shared_ptr<Pipeline> pipeline);

            void BindMaterial(std::shared_ptr<Material> mat);

            void BindPassData(const std::shared_ptr<RenderPass> pass, std::shared_ptr<Pipeline> pipeline);

            void BindPassData(const std::shared_ptr<RenderPass> pass, std::shared_ptr<Material> material);

            void BindLevelState(std::shared_ptr<Shader> shader, glm::mat4 modelMatrix, int objectID, bool applyPassGlobals);

            void DrawIndexed(const std::shared_ptr<Pipeline> pipeline, uint32_t indexCount, uint32_t indexOffset);
                
            void DrawFullScreenTriangle();
    };
}
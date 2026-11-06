#pragma once

#include <memory>
#include <random>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering {

    class IRenderContext;
    class Framebuffer;
    class Shader;
    class Pipeline;
    class Material;
    class Texture2D;

    // Depth + view-normal prepass, hemisphere-kernel AO, box blur. Passes always run, only the multiply in lit.frag is gated.
    // Init creates them all up front so the prepass exists before any world loads.
    class SSAOManager
    {
        public:
            void Init(IRenderContext* renderer, int width, int height);

            // Call again after Renderer::ClearPassesContent(), or AO stays 0 and lit.frag zeroes every ambient term.
            void SubmitFullscreenCommands(IRenderContext* renderer);

            void Update();

            void BindSSAOTexture(std::shared_ptr<Material> material);

            void RefreshSamplers(IRenderContext* renderer);

            std::shared_ptr<Framebuffer> GetPrepassFramebuffer() const { return m_PrepassFramebuffer; }

        private:
            void BuildKernelAndNoise();

            std::shared_ptr<Framebuffer> m_PrepassFramebuffer;
            std::shared_ptr<Framebuffer> m_RawAOFramebuffer;
            std::shared_ptr<Framebuffer> m_BlurFramebuffer;

            std::shared_ptr<Shader> m_PrepassShader;
            std::shared_ptr<Pipeline> m_PrepassPipeline;

            std::shared_ptr<Shader> m_SSAOShader;
            std::shared_ptr<Pipeline> m_SSAOPipeline;
            std::shared_ptr<Material> m_SSAOMaterial;

            std::shared_ptr<Shader> m_BlurShader;
            std::shared_ptr<Pipeline> m_BlurPipeline;
            std::shared_ptr<Material> m_BlurMaterial;

            std::shared_ptr<Texture2D> m_NoiseTexture;

            static constexpr int kKernelSize = 32;
            static constexpr int kNoiseTileSize = 4;

            std::mt19937 m_RNG{ 1234 }; // TEMP-SEED
    };
}

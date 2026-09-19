#pragma once

#include <memory>
#include <random>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering {

    class Renderer;
    class Framebuffer;
    class Shader;
    class Pipeline;
    class Material;
    class Texture2D;

    // Screen-space ambient occlusion, computed as three full-screen(-ish) passes ahead of the forward
    // pass : a depth+view-space-normal geometry prepass (the engine is a pure forward renderer with no
    // G-buffer otherwise - see the class comment on that shader pair), a hemisphere-kernel occlusion
    // pass sampling it, and a small box blur to remove the per-pixel noise-rotation dither pattern.
    // Mirrors ShadowManager/ProbeManager's shape : owns its own GPU resources, Init() once from
    // Renderer::Init(), Update() once per frame from Renderer::BeginFrame(), and a BindSSAOTexture(mat)
    // hook mirroring ShadowManager::BindShadowMaps, called from GLRendererAPI::ExecuteDrawCommand for
    // every regular (non-override-pipeline) draw.
    //
    // The 3 passes always run every frame regardless of any level's ssaoEnabled - only the multiply in
    // lit.frag is gated by that flag (see Level::ssaoEnabled). Every mesh gets submitted to the depth+
    // normal prepass the same way it's submitted to ForwardPass (see Model::AddToDrawList in
    // model_component.cpp), which means the prepass' RenderPass must already be registered by the time
    // any level loads - that's why Init() (not a lazily-created-on-first-use path like ProbeManager's
    // volumes) unconditionally creates all 3 passes up front.
    class SSAOManager
    {
        public:
            void Init(Renderer* renderer, int width, int height);

            // Submits the one permanent full-screen-triangle command each of the raw/blur passes draws.
            // These passes belong to the renderer, not to any level, but Renderer::ClearPassesContent()
            // (a level swap / play-mode reload) empties every pass's draw list - so it has to call this
            // again afterwards, otherwise both passes draw nothing, the AO target stays cleared to 0 and
            // lit.frag's SampleSSAO multiplies every ambient/indirect term (DDGI included) by 0.
            void SubmitFullscreenCommands(Renderer* renderer);

            // Refreshes this frame's view/projection/radius/bias/noise-tiling and re-dispatches the 3
            // passes via the render graph (actual dispatch happens later, in Renderer::DrawFrame(), when
            // the DAG execution order reaches these passes - this only updates their customUniforms).
            // Called once per frame from Renderer::BeginFrame(), alongside ProbeManager::Update().
            void Update();

            // Binds the final blurred AO texture onto `material`'s "ssaoTexture" sampler, mirroring
            // ShadowManager::BindShadowMaps - called once per regular forward-pass draw.
            void BindSSAOTexture(std::shared_ptr<Material> material);

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
            static constexpr int kNoiseTileSize = 4; // 4x4 tiled rotation texture

            std::mt19937 m_RNG{ std::random_device{}() };
    };
}

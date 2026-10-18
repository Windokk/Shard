#pragma once

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering {

    class IRenderContext;
    class Material;
    class Texture2D;
    class ComputeShader;
    class ComputePipeline;

    // Screen-space reflections : the specular light of a surface, taken from what the screen shows where its mirror ray lands, instead of from the
    // reflection probe or the sky (which do not know about the objects of the scene). Mirrors SSAOManager's shape : owns its GPU resources,
    // Init() once from Renderer::Init(), Update() once per frame from Renderer::BeginFrame(), and a BindSSR(mat) hook called with the scene's
    // other per-material state (Renderer::BindMaterialScene).
    //
    // It is a chain of compute dispatches, run as the "SSRPass" of the render graph (after the SSAO prepass, which it reads, and before whatever
    // lights the scene), in Execute() :
    //
    //   1. Hi-Z       a pyramid of the closest depth of the prepass (ssr_hiz_*.comp)
    //   2. Colour     a mip chain of the picture of the frame BEFORE (ssr_color_*.comp). The scene is lit after this runs, in the same frame, with
    //                 the result of this one : the colour a reflection shows is the last frame's, looked up where the camera had it
    //   3. Trace      the hierarchical march of every pixel's mirror ray, at 1 / 1, 2 or 4 of the screen (ssr_trace.comp), blended with the
    //                 frame before's hit when settings.temporal is on
    //   4. Resolve    one entry per pixel of the screen, edge aware upsample (ssr_resolve.comp)
    //
    // The result is read in lit.frag (SampleSSR) : a hit uv in the last picture + a blur size + a confidence per pixel, and the colour pyramid. The
    // trace does not know a surface's roughness (the prepass has no materials) : lit.frag, which does, picks the level of the pyramid with it, and
    // fades the reflection out of the surfaces too rough for it (SSRSettings::maxRoughness).
    //
    // Settings come from the world (SSRSettings, through the snapshot). Nothing here runs while they are off.
    class SSRManager
    {
        public:
            /// The name of the render graph pass this runs as
            static constexpr const char* kPassName = "SSRPass";

            /// Loads the shaders and registers the pass (after SSAOManager::Init : it comes after the prepass). When something fails IsReady()
            /// stays false and nothing is ever traced.
            void Init(IRenderContext* renderer);

            bool IsReady() const { return m_Ready; }

            /// Once per frame, before the passes run : switches the pass on or off with the world's settings, and keeps track of the camera
            void Update();

            /// The work of the pass. `sceneColor` is the GL texture that holds the last frame's resolved HDR scene.
            void Execute(uint32_t sceneColor);

            /// Gives the material's shader what lit.frag needs to read the reflections (or tells it there are none this frame). `allowed` is
            /// false for a view whose picture is not the screen's (the studio views).
            void BindSSR(const std::shared_ptr<Material>& material, bool allowed);

        private:
            bool EnsureResources(uint32_t width, uint32_t height, int divisor);

            /// A compute pass of the chain : binds its pipeline and returns the shader to set uniforms on
            void Dispatch(const std::shared_ptr<ComputePipeline>& pipeline, uint32_t width, uint32_t height);

            IRenderContext* m_Renderer = nullptr;
            bool m_Ready = false;

            std::shared_ptr<ComputeShader> m_HiZInitShader, m_HiZReduceShader, m_ColorInitShader, m_ColorReduceShader, m_TraceShader, m_ResolveShader;
            std::shared_ptr<ComputePipeline> m_HiZInitPipeline, m_HiZReducePipeline, m_ColorInitPipeline, m_ColorReducePipeline, m_TracePipeline, m_ResolvePipeline;

            std::shared_ptr<Texture2D> m_HiZ;               // R32F, closest depth, mips
            std::shared_ptr<Texture2D> m_Pyramid;           // RGBA16F, the last picture, mips
            std::shared_ptr<Texture2D> m_Result;            // RGBA16F, per pixel : hit uv, blur scale, confidence
            std::shared_ptr<Texture2D> m_History[2];        // RGBA32F at the trace's size : world hit, confidence. Flips every frame.
            uint32_t m_Width = 0, m_Height = 0;
            uint32_t m_TraceWidth = 0, m_TraceHeight = 0;
            int m_Divisor = 0;
            uint32_t m_HiZLevels = 0;                       // levels of the Hi-Z that are built (and that the march may use)
            uint32_t m_PyramidLevels = 0;                   // levels of the colour pyramid that are built
            uint64_t m_ResultHandle = 0;
            uint64_t m_PyramidHandle = 0;

            int m_HistoryIndex = 0;                         // the history the next trace writes
            bool m_HistoryValid = false;

            // The camera of this frame and the frame before's, whose picture the reflections are read from
            glm::mat4 m_ViewProj = glm::mat4(1.0f);
            glm::mat4 m_PrevViewProj = glm::mat4(1.0f);
            glm::vec3 m_PrevPosition = glm::vec3(0.0f);
            glm::vec3 m_PrevForward = glm::vec3(0.0f, 0.0f, -1.0f);
            bool m_HaveCamera = false;
            float m_CameraStill = 1.0f;

            bool m_Produced = false;                        // this frame's result exists
    };
}

#include "render_pipeline.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/world/engine.hpp"

namespace Shard::Engine::Rendering {

    namespace {

        constexpr const char* kForwardPass = "ForwardPass";
        constexpr const char* kGBufferPass = "GBufferPass";
        constexpr const char* kLightingPass = "DeferredLightingPass";

        /// The forward pass is the renderer's : a pipeline sets it up for what it makes of it
        void ConfigureForwardPass(Renderer& renderer, bool clear, bool depthPrepass)
        {
            if (std::shared_ptr<RenderPass> pass = renderer.GetRenderPass(kForwardPass))
            {
                pass->clearColor = clear;
                pass->clearDepth = clear;
                pass->depthPrepass = depthPrepass;
            }
        }

        // ---- Forward+ ----------------------------------------------------------------------------------------------------------

        /// Every mesh is drawn with its material, lit where it is drawn : the opaque geometry lays its depth down first (a depth sweep) so that each pixel
        /// is shaded once, the lights come from the clusters the light culling compute pass filled. Translucent geometry is blended on top. This is
        /// the pipeline the engine was built around : the passes it needs (the forward pass, the shadows, SSAO) are the renderer's.
        class ForwardPlusPipeline final : public RenderPipeline
        {
            public:

                const char* GetType() const override { return "forward+"; }

                void Activate(Renderer& renderer) override { ConfigureForwardPass(renderer, true, true); }
                void Deactivate(Renderer&) override {}
        };

        // ---- Deferred ----------------------------------------------------------------------------------------------------------

        /// The materials of the opaque and masked geometry are evaluated once per pixel into a G-buffer (the same lit shader, permutation GBUFFER),
        /// then one full screen pass lights every pixel from it (permutation SHARD_DEFERRED_LIGHTING : the same lighting code, ShadeSurface). Lighting cost
        /// no longer depends on how many layers of geometry overlap, and the cost of a material is paid once. The forward pass still runs afterwards, for
        /// what a G-buffer cannot hold : the translucent geometry, and the sky.
        ///
        /// The G-buffer is not multisampled : the geometry edges are not anti-aliased by this pipeline.
        class DeferredPipeline final : public RenderPipeline
        {
            public:

                const char* GetType() const override { return "deferred"; }

                void Activate(Renderer& renderer) override
                {
                    const glm::ivec2 size = renderer.GetViewportSize();

                    // RGBA8 albedo, then three RGBA16F : the normals (octahedral, shading and geometric), the emission with the metallic, the baked light with the roughness
                    FramebufferSpecifications specs;
                    specs.width = size.x;
                    specs.height = size.y;
                    specs.hasColor = true;
                    specs.hasDepth = true;
                    specs.multisampled = false;

                    auto nearest = [](TextureInternalFormat format) {
                        TextureSpecifications t;
                        t.internalFormat = format;
                        t.minFilter = TextureFilter::Nearest;
                        t.magFilter = TextureFilter::Nearest;
                        t.wrapS = TextureWrap::ClampEdge;
                        t.wrapT = TextureWrap::ClampEdge;
                        t.generateMips = false;
                        return t;
                    };

                    specs.colorSpecs = nearest(TextureInternalFormat::RGBA8);
                    specs.extraColorSpecs = {nearest(TextureInternalFormat::RGBA16F), nearest(TextureInternalFormat::RGBA16F), nearest(TextureInternalFormat::RGBA16F)};
                    specs.depthSpecs.internalFormat = TextureInternalFormat::Depth32F;

                    m_GBuffer = Framebuffer::Create(specs);

                    auto gbufferPass = std::make_shared<RenderPass>();
                    gbufferPass->target = m_GBuffer;
                    gbufferPass->clearColor = true;
                    gbufferPass->clearDepth = true;
                    gbufferPass->shaderVariant = "SHARD_GBUFFER";
                    renderer.AddRenderPass(gbufferPass, kGBufferPass, {});

                    // The lighting : a quad over the whole screen drawn with the lit shader's deferred permutation (a mesh, not a fullscreen triangle :
                    // the draws of meshes are the ones that get the scene's lights, shadows and probes bound)
                    const Filesystem::Path directory = Core::GetEngine().GetFileManager()->GetEngineResRoot() / "shaders/mesh";
                    std::shared_ptr<Shader> shader = Shader::Create(directory / "deferred_lighting.vert", directory / "lit.frag", Filesystem::Path(""), {"SHARD_DEFERRED_LIGHTING"});
                    if (!shader)
                    {
                        DEBUG_ERROR("The deferred lighting shader could not be loaded : the deferred pipeline draws nothing.");
                        return;
                    }

                    PipelineSpecifications pipeline;
                    pipeline.shader = shader;
                    pipeline.topology = PrimitiveTopology::Triangles;
                    pipeline.cullMode = CullMode::None;
                    // The depth of the opaque geometry is written into the scene's depth, for what is drawn after (the translucent geometry, the sky,
                    // the overlays) : the test that lets a draw write depth has to be on, and passes always
                    pipeline.depthTest = true;
                    pipeline.depthWrite = true;
                    pipeline.depthCompare = DepthCompareOp::Always;
                    pipeline.blending = false;
                    pipeline.debugName = "DeferredLightingPipeline";

                    m_Material = Material::Create(shader, renderer.GetOrAddPipeline(pipeline), true, Opacity::Opaque);

                    auto lightingPass = std::make_shared<RenderPass>();
                    lightingPass->target = renderer.GetSceneFramebuffer();
                    lightingPass->clearColor = true;
                    lightingPass->clearDepth = true;
                    lightingPass->allowResize = false;
                    lightingPass->allowCulling = false;
                    renderer.AddRenderPass(lightingPass, kLightingPass, {kGBufferPass});

                    // The sky and the translucent geometry go on top of the lit picture
                    ConfigureForwardPass(renderer, false, false);

                    SubmitLightingCommand(renderer);
                }

                void Deactivate(Renderer& renderer) override
                {
                    ConfigureForwardPass(renderer, true, true);

                    if (renderer.HasRenderPass(kLightingPass))
                        renderer.RemoveRenderPass(kLightingPass);
                    if (renderer.HasRenderPass(kGBufferPass))
                        renderer.RemoveRenderPass(kGBufferPass);

                    if (m_GBuffer)
                    {
                        m_GBuffer->Destroy();
                        m_GBuffer.reset();
                    }
                    m_Material.reset();
                }

                void BeforeFrame(Renderer& renderer, const RenderSnapshot& snapshot) override
                {
                    std::shared_ptr<RenderPass> pass = renderer.GetRenderPass(kLightingPass);
                    if (!pass || !m_GBuffer)
                        return;

                    // The camera's matrices come with the draw (the camera state) ; what the shader needs on top is where a pixel is in the world
                    pass->customUniforms["uInvViewProjection"] = glm::inverse(snapshot.camera.viewProjection);

                    pass->customSamplers["gAlbedoTex"] = m_GBuffer->GetColorAttachmentAt(0);
                    pass->customSamplers["gNormalsTex"] = m_GBuffer->GetColorAttachmentAt(1);
                    pass->customSamplers["gMaterialTex"] = m_GBuffer->GetColorAttachmentAt(2);
                    pass->customSamplers["gBakedTex"] = m_GBuffer->GetColorAttachmentAt(3);
                    pass->customSamplers["gDepthTex"] = m_GBuffer->GetDepthAttachment();
                }

                void OnPassesCleared(Renderer& renderer) override { SubmitLightingCommand(renderer); }

                std::string Route(const std::string& requested, const DrawCommand& command) const override
                {
                    if (requested != kForwardPass)
                        return requested;

                    // The sky is a full screen triangle that sits behind everything (it is drawn first, geometry covers it) : it goes in the lighting
                    // pass, ahead of the lighting quad, which leaves the pixels without geometry alone
                    if (command.fullscreenTri)
                        return kLightingPass;

                    // The scene submits to the forward pass : what the G-buffer can hold goes there instead
                    if (command.material && command.material->GetOpacity() != Opacity::Translucent)
                        return kGBufferPass;
                    return requested;
                }

                void RouteRemoval(const std::string& requested, std::vector<std::string>& passes) const override
                {
                    passes.push_back(requested);
                    if (requested == kForwardPass)
                    {
                        passes.push_back(kGBufferPass);
                        passes.push_back(kLightingPass);
                    }
                }

                void AdjustDependencies(std::unordered_map<std::string, std::vector<std::string>>& dependencies) const override
                {
                    // What the forward pass waited for (the shadow maps, the AO) is what the lighting needs : the forward pass now comes after the lighting
                    std::vector<std::string> lighting = {kGBufferPass};
                    for (const std::string& dependency : dependencies[kForwardPass])
                    {
                        if (dependency != kLightingPass && dependency != kGBufferPass)
                            lighting.push_back(dependency);
                    }

                    dependencies[kLightingPass] = lighting;
                    dependencies[kForwardPass] = {kLightingPass};
                }

                bool SupportsView(const DebugViewState& view) const override
                {
                    return view.mode == ViewMode::Lit && !view.wireframeRaster;
                }

            private:

                void SubmitLightingCommand(Renderer& renderer)
                {
                    if (!m_Material)
                        return;

                    DrawCommand command{};
                    command.mesh = renderer.GetUnitQuad();
                    command.material = m_Material;
                    command.modelMatrix = glm::mat4(1.0f);
                    command.indexCount = 6;
                    command.vertexCount = 4;
                    command.objectID = 0;
                    command.bindCameraState = true;
                    renderer.AddOrUpdateCommands({command}, {kLightingPass}, false);
                }

                std::shared_ptr<Framebuffer> m_GBuffer;
                std::shared_ptr<Material> m_Material;
        };
    }

    void RegisterBuiltinRenderPipelines()
    {
        RenderPipelineRegistry& registry = RenderPipelineRegistry::Get();

        registry.RegisterType("forward+", "Forward+ : every mesh lit where it is drawn, the lights clustered, a depth sweep so that each pixel is shaded once",
            [](const nlohmann::json&) { return std::make_unique<ForwardPlusPipeline>(); });

        registry.RegisterType("deferred", "Deferred : the materials go to a G-buffer, one full screen pass lights it (not anti-aliased on geometry edges)",
            [](const nlohmann::json&) { return std::make_unique<DeferredPipeline>(); });
    }

}

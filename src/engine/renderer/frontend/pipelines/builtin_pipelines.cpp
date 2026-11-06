#include "render_pipeline.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/world/engine.hpp"

namespace Shard::Engine::Rendering {

    namespace {

        constexpr const char* kForwardPass = "ForwardPass";
        constexpr const char* kGBufferPass = "GBufferPass";
        constexpr const char* kLightingPass = "DeferredLightingPass";
        constexpr const char* kWireOverlayPass = "DeferredWireOverlayPass";

        void ConfigureForwardPass(Renderer& renderer, bool clear, bool depthPrepass)
        {
            if (std::shared_ptr<RenderPass> pass = renderer.GetRenderPass(kForwardPass))
            {
                pass->clearColor = clear;
                pass->clearDepth = clear;
                pass->depthPrepass = depthPrepass;
            }
        }

        class ForwardPlusPipeline final : public RenderPipeline
        {
            public:

                const char* GetType() const override { return "forward+"; }

                void Activate(Renderer& renderer) override { ConfigureForwardPass(renderer, true, true); }
                void Deactivate(Renderer&) override {}
        };

        // G-buffer comes from the lit shader's GBUFFER permutation, lit once per MSAA sample. Forward pass still draws translucent and sky.
        class DeferredPipeline final : public RenderPipeline
        {
            public:

                const char* GetType() const override { return "deferred"; }

                void Activate(Renderer& renderer) override
                {
                    const glm::ivec2 size = renderer.GetViewportSize();

                    // RGBA8 albedo + 3x RGBA16F: octahedral normals (shading, geometric), emission+metallic, baked light+roughness.
                    FramebufferSpecifications specs;
                    specs.width = size.x;
                    specs.height = size.y;
                    specs.hasColor = true;
                    specs.hasDepth = true;

                    const FramebufferSpecifications sceneSpecs = renderer.GetSceneFramebuffer()->GetSpecifications();
                    m_Multisampled = sceneSpecs.multisampled && sceneSpecs.samplesCount > 1;
                    specs.multisampled = m_Multisampled;
                    specs.samplesCount = sceneSpecs.samplesCount;

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
                    gbufferPass->debugViewRole = DebugViewRole::GBuffer;
                    renderer.AddRenderPass(gbufferPass, kGBufferPass, {});

                    const Filesystem::Path directory = Core::GetEngine().GetFileManager()->GetEngineResRoot() / "shaders/mesh";
                    std::vector<std::string> defines = {"SHARD_DEFERRED_LIGHTING"};
                    if (m_Multisampled)
                        defines.push_back("SHARD_GBUFFER_MS");
                    std::shared_ptr<Shader> shader = Shader::Create(directory / "deferred_lighting.vert", directory / "lit.frag", Filesystem::Path(""), defines);
                    if (!shader)
                    {
                        DEBUG_ERROR("The deferred lighting shader could not be loaded : the deferred pipeline draws nothing.");
                        return;
                    }

                    PipelineSpecifications pipeline;
                    pipeline.shader = shader;
                    pipeline.topology = PrimitiveTopology::Triangles;
                    pipeline.cullMode = CullMode::None;
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
                    lightingPass->debugViewRole = DebugViewRole::Lighting;
                    renderer.AddRenderPass(lightingPass, kLightingPass, {kGBufferPass});

                    auto wirePass = std::make_shared<RenderPass>();
                    wirePass->target = renderer.GetSceneFramebuffer();
                    wirePass->clearColor = false;
                    wirePass->clearDepth = false;
                    wirePass->allowResize = false;
                    wirePass->externalDrawList = &gbufferPass->drawList;
                    wirePass->debugViewRole = DebugViewRole::WireOverlay;
                    wirePass->enabled = false;
                    renderer.AddRenderPass(wirePass, kWireOverlayPass, {kLightingPass});

                    ConfigureForwardPass(renderer, false, false);

                    SubmitLightingCommand(renderer);
                }

                void Deactivate(Renderer& renderer) override
                {
                    ConfigureForwardPass(renderer, true, true);

                    if (renderer.HasRenderPass(kWireOverlayPass))
                        renderer.RemoveRenderPass(kWireOverlayPass);
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

                    if (m_Multisampled)
                    {
                        const uint32_t sceneSamples = renderer.GetSceneFramebuffer()->GetSpecifications().samplesCount;
                        if (m_GBuffer->GetSpecifications().samplesCount != sceneSamples)
                            m_GBuffer->SetSamplesCount(sceneSamples);
                    }

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

                    if (command.fullscreenTri)
                        return kLightingPass;

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
                    std::vector<std::string> lighting = {kGBufferPass};
                    for (const std::string& dependency : dependencies[kForwardPass])
                    {
                        if (dependency != kLightingPass && dependency != kGBufferPass)
                            lighting.push_back(dependency);
                    }

                    dependencies[kLightingPass] = lighting;
                    dependencies[kWireOverlayPass] = {kLightingPass};
                    dependencies[kForwardPass] = {kLightingPass, kWireOverlayPass};
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
                bool m_Multisampled = false;
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

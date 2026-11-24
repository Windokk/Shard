#include "render_pipeline.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/config/cvar.hpp"
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
        constexpr const char* kEdgeMaskPass = "DeferredEdgeMaskPass";
        constexpr const char* kLightingEdgePass = "DeferredLightingEdgePass";

        Core::CVar<bool> cvMsaaEdgeLighting("render.msaa_edge_lighting", true,
            "Deferred with MSAA : light the pixels whose samples show one surface once, and only the edge pixels once per sample (off : every pixel once per sample, about 4 times the lighting cost at 4x ; applies when the render pipeline is activated)");

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
                    gbufferPass->shaderVariant = "SHARD_GBUFFER";                    gbufferPass->debugViewRole = DebugViewRole::GBuffer;
                    renderer.AddRenderPass(gbufferPass, kGBufferPass, {});

                    const Filesystem::Path directory = Core::GetEngine().GetFileManager()->GetEngineResRoot() / "shaders/mesh";
                    // With MSAA the lighting is cut in two draws (see lit.frag) : the pixels of one surface lit once, the edge pixels lit per sample.
                    m_Split = m_Multisampled && cvMsaaEdgeLighting.Get() && CreateEdgeMask(renderer, directory, size, nearest(TextureInternalFormat::R16F));

                    std::vector<std::string> defines = {"SHARD_DEFERRED_LIGHTING"};
                    if (m_Multisampled)
                        defines.push_back("SHARD_GBUFFER_MS");
                    if (m_Split)
                        defines.push_back("SHARD_GBUFFER_SPLIT");
                    std::shared_ptr<Shader> shader = Shader::Create(directory / "deferred_lighting.vert", directory / "lit.frag", Filesystem::Path(""), defines);
                    if (!shader)
                    {
                        DEBUG_ERROR("The deferred lighting shader could not be loaded : the deferred pipeline draws nothing.");
                        m_Split = false;
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

                    if (m_Split)
                    {
                        // A material of its own, not a variant of the first : the SSAO and SSR bindings write to the shader of the material they are given, and only that one would get them.
                        defines.push_back("SHARD_GBUFFER_INTERIOR");
                        std::shared_ptr<Shader> interiorShader = Shader::Create(directory / "deferred_lighting.vert", directory / "lit.frag", Filesystem::Path(""), defines);
                        if (interiorShader)
                        {
                            pipeline.shader = interiorShader;
                            pipeline.debugName = "DeferredLightingInteriorPipeline";
                            m_InteriorMaterial = Material::Create(interiorShader, renderer.GetOrAddPipeline(pipeline), true, Opacity::Opaque);
                        }
                        if (!m_InteriorMaterial)
                        {
                            DEBUG_ERROR("The deferred lighting shader (interior pixels) could not be loaded : the deferred pipeline draws nothing.");
                            m_Split = false;
                            return;
                        }
                    }

                    auto lightingPass = std::make_shared<RenderPass>();
                    lightingPass->target = renderer.GetSceneFramebuffer();
                    lightingPass->clearColor = true;
                    lightingPass->clearDepth = true;
                    lightingPass->allowResize = false;
                    lightingPass->allowCulling = false;
                    lightingPass->debugViewRole = DebugViewRole::Lighting;
                    renderer.AddRenderPass(lightingPass, kLightingPass, {kGBufferPass});

                    if (m_Split)
                    {
                        // Which pixels have samples that differ, then the lighting of the pixels that do not (above), then of the ones that do, over it.
                        auto maskPass = std::make_shared<RenderPass>();
                        maskPass->target = m_EdgeMask;
                        maskPass->clearColor = false;
                        maskPass->clearDepth = false;
                        maskPass->allowCulling = false;
                        renderer.AddRenderPass(maskPass, kEdgeMaskPass, {kGBufferPass});

                        auto edgePass = std::make_shared<RenderPass>();
                        edgePass->target = renderer.GetSceneFramebuffer();
                        edgePass->clearColor = false;
                        edgePass->clearDepth = false;
                        edgePass->allowResize = false;
                        edgePass->allowCulling = false;
                        edgePass->debugViewRole = DebugViewRole::Lighting;
                        renderer.AddRenderPass(edgePass, kLightingEdgePass, {kLightingPass});
                    }

                    auto wirePass = std::make_shared<RenderPass>();
                    wirePass->target = renderer.GetSceneFramebuffer();
                    wirePass->clearColor = false;
                    wirePass->clearDepth = false;
                    wirePass->allowResize = false;
                    wirePass->externalDrawList = &gbufferPass->drawList;
                    wirePass->debugViewRole = DebugViewRole::WireOverlay;
                    wirePass->enabled = false;
                    renderer.AddRenderPass(wirePass, kWireOverlayPass, {m_Split ? kLightingEdgePass : kLightingPass});

                    ConfigureForwardPass(renderer, false, false);

                    SubmitLightingCommand(renderer);
                }

                void Deactivate(Renderer& renderer) override
                {
                    ConfigureForwardPass(renderer, true, true);

                    for (const char* name : {kWireOverlayPass, kLightingEdgePass, kLightingPass, kEdgeMaskPass, kGBufferPass})
                        if (renderer.HasRenderPass(name))
                            renderer.RemoveRenderPass(name);

                    if (m_GBuffer)
                    {
                        m_GBuffer->Destroy();
                        m_GBuffer.reset();
                    }
                    if (m_EdgeMask)
                    {
                        m_EdgeMask->Destroy();
                        m_EdgeMask.reset();
                    }
                    m_Material.reset();
                    m_InteriorMaterial.reset();
                    m_MaskMaterial.reset();
                    m_Split = false;
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

                    const glm::mat4 invViewProjection = glm::inverse(snapshot.camera.viewProjection);

                    auto bindGBuffer = [&](const std::shared_ptr<RenderPass>& target) {
                        target->customUniforms["uInvViewProjection"] = invViewProjection;

                        target->customSamplers["gAlbedoTex"] = m_GBuffer->GetColorAttachmentAt(0);
                        target->customSamplers["gNormalsTex"] = m_GBuffer->GetColorAttachmentAt(1);
                        target->customSamplers["gMaterialTex"] = m_GBuffer->GetColorAttachmentAt(2);
                        target->customSamplers["gBakedTex"] = m_GBuffer->GetColorAttachmentAt(3);
                        target->customSamplers["gDepthTex"] = m_GBuffer->GetDepthAttachment();
                    };

                    bindGBuffer(pass);

                    if (!m_Split)
                        return;

                    pass->customSamplers["gEdgeMaskTex"] = m_EdgeMask->GetColorAttachment();

                    if (std::shared_ptr<RenderPass> edgePass = renderer.GetRenderPass(kLightingEdgePass))
                    {
                        bindGBuffer(edgePass);
                        edgePass->customSamplers["gEdgeMaskTex"] = m_EdgeMask->GetColorAttachment();
                    }

                    if (std::shared_ptr<RenderPass> maskPass = renderer.GetRenderPass(kEdgeMaskPass))
                    {
                        bindGBuffer(maskPass);
                        maskPass->customUniforms["uGBufferSamples"] = int(m_GBuffer->GetSpecifications().samplesCount);
                    }
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
                        if (m_Split)
                        {
                            passes.push_back(kEdgeMaskPass);
                            passes.push_back(kLightingEdgePass);
                        }
                    }
                }

                void AdjustDependencies(std::unordered_map<std::string, std::vector<std::string>>& dependencies) const override
                {
                    // The lighting waits for what the forward pass waited for (shadow maps, SSAO ...) : those are its inputs.
                    std::vector<std::string> lighting = {kGBufferPass};
                    if (m_Split)
                        lighting.push_back(kEdgeMaskPass);
                    for (const std::string& dependency : dependencies[kForwardPass])
                    {
                        if (dependency != kLightingPass && dependency != kGBufferPass && dependency != kEdgeMaskPass && dependency != kLightingEdgePass)
                            lighting.push_back(dependency);
                    }

                    dependencies[kLightingPass] = lighting;
                    if (m_Split)
                    {
                        dependencies[kEdgeMaskPass] = {kGBufferPass};
                        dependencies[kLightingEdgePass] = {kLightingPass};
                    }

                    const char* lastLighting = m_Split ? kLightingEdgePass : kLightingPass;
                    dependencies[kWireOverlayPass] = {lastLighting};
                    dependencies[kForwardPass] = {lastLighting, kWireOverlayPass};
                }

            private:

                // The mask of the pixels whose samples differ : its shader, its target and its material. The passes come with the lighting ones.
                bool CreateEdgeMask(Renderer& renderer, const Filesystem::Path& directory, const glm::ivec2& size, const TextureSpecifications& format)
                {
                    std::shared_ptr<Shader> shader = Shader::Create(directory / "deferred_lighting.vert", directory / "gbuffer_edge_mask.frag", Filesystem::Path(""), {});
                    if (!shader)
                    {
                        DEBUG_WARNING("The G-buffer edge mask shader could not be loaded : the deferred lighting shades every MSAA sample of every pixel.");
                        return false;
                    }

                    FramebufferSpecifications maskSpecs;
                    maskSpecs.width = size.x;
                    maskSpecs.height = size.y;
                    maskSpecs.hasColor = true;
                    maskSpecs.hasDepth = false;
                    maskSpecs.colorSpecs = format;
                    m_EdgeMask = Framebuffer::Create(maskSpecs);
                    if (!m_EdgeMask)
                        return false;

                    PipelineSpecifications pipeline;
                    pipeline.shader = shader;
                    pipeline.topology = PrimitiveTopology::Triangles;
                    pipeline.cullMode = CullMode::None;
                    pipeline.depthTest = false;
                    pipeline.depthWrite = false;
                    pipeline.blending = false;
                    pipeline.debugName = "DeferredEdgeMaskPipeline";

                    m_MaskMaterial = Material::Create(shader, renderer.GetOrAddPipeline(pipeline), false, Opacity::Opaque);
                    return m_MaskMaterial != nullptr;
                }

                void SubmitLightingCommand(Renderer& renderer)
                {
                    if (!m_Material)
                        return;

                    DrawCommand command{};
                    command.mesh = renderer.GetUnitQuad();
                    command.material = m_Split ? m_InteriorMaterial : m_Material;
                    command.modelMatrix = glm::mat4(1.0f);
                    command.indexCount = 6;
                    command.vertexCount = 4;
                    command.objectID = 0;
                    command.bindCameraState = true;
                    renderer.AddOrUpdateCommands({command}, {kLightingPass}, false);

                    if (!m_Split || !m_MaskMaterial)
                        return;

                    command.material = m_Material;
                    renderer.AddOrUpdateCommands({command}, {kLightingEdgePass}, false);

                    DrawCommand maskCommand = command;
                    maskCommand.material = m_MaskMaterial;
                    maskCommand.bindCameraState = false;
                    renderer.AddOrUpdateCommands({maskCommand}, {kEdgeMaskPass}, false);
                }

                std::shared_ptr<Framebuffer> m_GBuffer;
                std::shared_ptr<Framebuffer> m_EdgeMask;
                std::shared_ptr<Material> m_Material;
                std::shared_ptr<Material> m_InteriorMaterial;
                std::shared_ptr<Material> m_MaskMaterial;
                bool m_Multisampled = false;
                bool m_Split = false;
        };
    }

    void RegisterBuiltinRenderPipelines()
    {
        RenderPipelineRegistry& registry = RenderPipelineRegistry::Get();

        registry.RegisterType("forward+", "Forward+ : every mesh lit where it is drawn, the lights clustered, a depth sweep so that each pixel is shaded once",
            [](const nlohmann::json&) { return std::make_unique<ForwardPlusPipeline>(); });

        registry.RegisterType("deferred", "Deferred : the materials go to a G-buffer, one full screen pass lights it (with MSAA on, once per sample, so the edges are anti-aliased). Translucent geometry and the sky are forward",
            [](const nlohmann::json&) { return std::make_unique<DeferredPipeline>(); });
    }

}

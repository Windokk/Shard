#include "ssao_manager.hpp"

#include "engine/core/engine.hpp"
#include "engine/core/resources/resources_manager.hpp"

#include "engine/rendering/renderer/renderer.hpp"
#include "engine/rendering/shader/shader.hpp"
#include "engine/rendering/material/material.hpp"
#include "engine/rendering/pipeline/pipeline.hpp"
#include "engine/rendering/framebuffer/framebuffer.hpp"
#include "engine/rendering/texture/texture.hpp"
#include "engine/rendering/mesh/mesh.hpp"

#include "engine/objects/components/rendering/camera.hpp"
#include "engine/rendering/camera/camera_manager.hpp"

#include "engine/levels/level.hpp"
#include "engine/levels/level_manager.hpp"

#include <glm/gtc/random.hpp>

namespace Shard::Engine::Rendering {

    namespace {

        struct Vertex
        {
            glm::vec3 position;
            glm::vec2 texCoord;
            glm::vec3 normal;
            glm::vec4 color;
            glm::vec3 tangent;
        };

    }

    void SSAOManager::Init(Renderer* renderer, int width, int height)
    {
        // ---- Framebuffers ----

        FramebufferSpecifications prepassSpecs;
        prepassSpecs.width = width;
        prepassSpecs.height = height;
        prepassSpecs.hasColor = true;
        prepassSpecs.hasDepth = true;
        prepassSpecs.colorSpecs.internalFormat = TextureInternalFormat::RGB16F; // view-space normal, signed
        m_PrepassFramebuffer = Framebuffer::Create(prepassSpecs);

        FramebufferSpecifications aoSpecs;
        aoSpecs.width = width;
        aoSpecs.height = height;
        aoSpecs.hasColor = true;
        aoSpecs.hasDepth = false;
        aoSpecs.colorSpecs.internalFormat = TextureInternalFormat::R16F; // single-channel occlusion
        m_RawAOFramebuffer = Framebuffer::Create(aoSpecs);
        m_BlurFramebuffer = Framebuffer::Create(aoSpecs);

        // ---- Depth+normal prepass (real scene geometry, RenderPass::overridePipeline - see the class
        // comment) ----

        m_PrepassShader = Core::GetEngine().GetResourcesManager()->GetShader("shaders/ssao/depth_normal");

        PipelineSpecifications prepassPipelineSpecs;
        prepassPipelineSpecs.shader = m_PrepassShader;
        prepassPipelineSpecs.debugName = "SSAODepthNormalPrepass";
        prepassPipelineSpecs.cullMode = CullMode::Back;
        prepassPipelineSpecs.depthTest = true;
        prepassPipelineSpecs.depthWrite = true;
        prepassPipelineSpecs.vertexLayout = VertexLayout{
            {
                {"aPos", ShaderDataType::Vec3, 0, offsetof(Vertex, position)},
                {"aNormal", ShaderDataType::Vec3, 2, offsetof(Vertex, normal)}
            },
            sizeof(Vertex)
        };
        m_PrepassPipeline = renderer->GetOrAddPipeline(prepassPipelineSpecs);

        std::shared_ptr<RenderPass> prepass = std::make_shared<RenderPass>();
        prepass->target = m_PrepassFramebuffer;
        prepass->clearColor = true;
        prepass->clearDepth = true;
        prepass->overridePipeline = true;
        prepass->customPipeline = m_PrepassPipeline;
        // allowCulling left at its default (true) - unlike shadow passes, this shares the main camera's
        // own frustum, so main-camera culling is exactly what's wanted here.
        renderer->AddRenderPass(prepass, "SSAODepthNormalPass", {});

        // ---- Raw AO pass (full-screen triangle sampling the prepass) ----

        m_SSAOShader = Core::GetEngine().GetResourcesManager()->GetShader("shaders/ssao/ssao");

        PipelineSpecifications ssaoPipelineSpecs;
        ssaoPipelineSpecs.shader = m_SSAOShader;
        ssaoPipelineSpecs.debugName = "SSAORawPass";
        ssaoPipelineSpecs.depthTest = false;
        ssaoPipelineSpecs.depthWrite = false;
        ssaoPipelineSpecs.vertexLayout = {};
        m_SSAOPipeline = renderer->GetOrAddPipeline(ssaoPipelineSpecs);

        m_SSAOMaterial = Material::Create(m_SSAOShader, m_SSAOPipeline, false, Opacity::Opaque);

        std::shared_ptr<RenderPass> rawPass = std::make_shared<RenderPass>();
        rawPass->target = m_RawAOFramebuffer;
        rawPass->clearColor = true;
        rawPass->overridePipeline = true;
        rawPass->customPipeline = m_SSAOPipeline;
        rawPass->customSamplers.emplace("gDepth", m_PrepassFramebuffer->GetDepthAttachment());
        rawPass->customSamplers.emplace("gNormal", m_PrepassFramebuffer->GetColorAttachment());

        renderer->AddRenderPass(rawPass, "SSAORawPass", {"SSAODepthNormalPass"});

        // ---- Blur pass (full-screen triangle sampling the raw AO pass) ----

        m_BlurShader = Core::GetEngine().GetResourcesManager()->GetShader("shaders/ssao/ssao_blur");

        PipelineSpecifications blurPipelineSpecs;
        blurPipelineSpecs.shader = m_BlurShader;
        blurPipelineSpecs.debugName = "SSAOBlurPass";
        blurPipelineSpecs.depthTest = false;
        blurPipelineSpecs.depthWrite = false;
        blurPipelineSpecs.vertexLayout = {};
        m_BlurPipeline = renderer->GetOrAddPipeline(blurPipelineSpecs);

        m_BlurMaterial = Material::Create(m_BlurShader, m_BlurPipeline, false, Opacity::Opaque);

        std::shared_ptr<RenderPass> blurPass = std::make_shared<RenderPass>();
        blurPass->target = m_BlurFramebuffer;
        blurPass->clearColor = true;
        blurPass->overridePipeline = true;
        blurPass->customPipeline = m_BlurPipeline;
        blurPass->customSamplers.emplace("ssaoInput", m_RawAOFramebuffer->GetColorAttachment());
        blurPass->barrierAfter = MemoryBarrierBit::TextureFetch;

        renderer->AddRenderPass(blurPass, "SSAOBlurPass", {"SSAORawPass"});
        renderer->AddDependencyToPass("ForwardPass", "SSAOBlurPass");

        SubmitFullscreenCommands(renderer);

        BuildKernelAndNoise();
    }

    void SSAOManager::SubmitFullscreenCommands(Renderer* renderer)
    {
        DrawCommand ssaoCmd{};
        ssaoCmd.fullscreenTri = true;
        ssaoCmd.bindCameraState = false;
        ssaoCmd.material = m_SSAOMaterial;
        renderer->AddOrUpdateCommands({ssaoCmd}, {"SSAORawPass"}, false);

        DrawCommand blurCmd{};
        blurCmd.fullscreenTri = true;
        blurCmd.bindCameraState = false;
        blurCmd.material = m_BlurMaterial;
        renderer->AddOrUpdateCommands({blurCmd}, {"SSAOBlurPass"}, false);
    }

    void SSAOManager::BuildKernelAndNoise()
    {
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);

        m_SSAOPipeline->Bind();

        for (int i = 0; i < kKernelSize; i++)
        {
            glm::vec3 sample(unit(m_RNG) * 2.0f - 1.0f, unit(m_RNG) * 2.0f - 1.0f, unit(m_RNG));
            sample = glm::normalize(sample) * unit(m_RNG);

            float scale = float(i) / float(kKernelSize);
            scale = glm::mix(0.1f, 1.0f, scale * scale);
            sample *= scale;

            m_SSAOShader->SetVec3("samples[" + std::to_string(i) + "]", sample);
        }

        std::vector<glm::vec3> noiseData;
        noiseData.reserve(kNoiseTileSize * kNoiseTileSize);
        for (int i = 0; i < kNoiseTileSize * kNoiseTileSize; i++)
            noiseData.emplace_back(unit(m_RNG) * 2.0f - 1.0f, unit(m_RNG) * 2.0f - 1.0f, 0.0f);

        TextureSpecifications noiseSpecs;
        noiseSpecs.width = kNoiseTileSize;
        noiseSpecs.height = kNoiseTileSize;
        noiseSpecs.internalFormat = TextureInternalFormat::RGB16F;
        noiseSpecs.minFilter = TextureFilter::Nearest;
        noiseSpecs.magFilter = TextureFilter::Nearest;
        noiseSpecs.wrapS = TextureWrap::Repeat;
        noiseSpecs.wrapT = TextureWrap::Repeat;
        noiseSpecs.generateMips = false;
        m_NoiseTexture = Texture2D::Create(noiseSpecs, noiseData.data());

        auto renderer = Core::GetEngine().GetRenderer();
        renderer->GetRenderPass("SSAORawPass")->customSamplers["noiseTex"] = m_NoiseTexture->GetHandle();
    }

    void SSAOManager::Update()
    {
        auto renderer = Core::GetEngine().GetRenderer();

        auto level = Core::GetEngine().GetLevelManager()->GetLevelAt(0);
        if (!level)
            return;

        auto cam = Core::GetEngine().GetCameraManager()->GetActiveCamera();
        if (!cam)
            return;

        glm::mat4 view = cam->GetView();
        glm::mat4 projection = cam->GetProjection();

        auto prepass = renderer->GetRenderPass("SSAODepthNormalPass");
        prepass->customUniforms["view"] = view;
        prepass->customUniforms["projection"] = projection;

        auto rawPass = renderer->GetRenderPass("SSAORawPass");
        rawPass->customUniforms["projection"] = projection;
        rawPass->customUniforms["radius"] = level->ssaoRadius;
        rawPass->customUniforms["bias"] = level->ssaoBias;
        rawPass->customUniforms["power"] = level->ssaoPower;
        rawPass->customUniforms["noiseScale"] = glm::vec2(
            float(m_PrepassFramebuffer->GetWidth()) / float(kNoiseTileSize),
            float(m_PrepassFramebuffer->GetHeight()) / float(kNoiseTileSize)
        );
    }

    void SSAOManager::BindSSAOTexture(std::shared_ptr<Material> material)
    {
        if (!m_BlurFramebuffer)
            return;

        uint64_t handle = m_BlurFramebuffer->GetColorAttachmentBindlessHandle();
        material->GetShader()->SetUVec2("ssaoTextureHandle",
            (uint32_t)(handle & 0xFFFFFFFFu),
            (uint32_t)(handle >> 32));
    }

}

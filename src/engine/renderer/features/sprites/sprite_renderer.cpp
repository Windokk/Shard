#include "sprite_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/render_pass.hpp"
#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/world/engine.hpp"

namespace Shard::Engine::Rendering {

    namespace {
        constexpr uint32_t kInstancesBinding = 25;
        constexpr uint32_t kMinCapacity = 256;
        constexpr uint32_t kMaxSprites = 262144;

        // SHARD_BLEND_* (shard_render.h) -> the pass group's blend : 0 opaque and cutout, 1 alpha, 2 additive, 3 premultiplied
        uint32_t BlendGroup(uint8_t blend)
        {
            switch (blend) {
                case 0: case 1: return 0;
                case 3: return 2;
                case 4: return 3;
                default: return 1;
            }
        }

        // Group g = blendGroup * 2 + (depth test ? 0 : 1)
        const char* kPassNames[SpriteRenderer::kGroups] = {
            "SpriteOpaquePass", "SpriteOpaqueOverlayPass", "SpriteAlphaPass", "SpriteAlphaOverlayPass",
            "SpriteAdditivePass", "SpriteAdditiveOverlayPass", "SpritePremultipliedPass", "SpritePremultipliedOverlayPass" };

        uint64_t CommandIdOf(uint32_t group)
        {
            constexpr uint64_t kSpriteComponentID = 0x53505254;     // "SPRT"
            return MakeCommandID(0, kSpriteComponentID, group);
        }
    }

    SpriteRenderer::SpriteRenderer() = default;
    SpriteRenderer::~SpriteRenderer() = default;

    void SpriteRenderer::Build(const RenderSnapshot& snapshot, std::vector<SpriteGpu>& out, Range (&ranges)[kGroups])
    {
        out.clear();
        for (Range& r : ranges) r = Range{};

        const glm::vec3 camera = snapshot.camera.valid ? snapshot.camera.position : glm::vec3(0.0f);

        // Group by pass, keeping the batches in the order of their keys inside a group
        std::vector<const SpriteBatchProxy*> groups[kGroups];
        for (const SpriteBatchProxy& batch : snapshot.spriteBatches) {
            if ((batch.flags & 0x10) || batch.sprites.empty())      // SHARD_SPRITE_HIDDEN
                continue;
            const bool depthTest = (batch.flags & 0x01) != 0;       // SHARD_SPRITE_DEPTH_TEST
            groups[BlendGroup(batch.blend) * 2 + (depthTest ? 0 : 1)].push_back(&batch);
        }

        uint32_t total = 0;
        for (uint32_t g = 0; g < kGroups; ++g) {
            ranges[g].first = total;
            for (const SpriteBatchProxy* batch : groups[g]) {
                const uint64_t texture = batch->texture ? batch->texture->GetBindlessHandle() : 0;
                // A batch whose texture the API cannot hand to a shader is drawn with its colours only
                const uint32_t flags = ((batch->flags & 0x04) ? 1u : 0u)                    // SHARD_SPRITE_SCREEN_SIZE
                                     | (batch->blend == 1 ? 2u : 0u)                        // cut out
                                     | (batch->blend == 0 ? 4u : 0u);                       // opaque
                const size_t begin = out.size();
                for (const SpriteInstance& s : batch->sprites) {
                    if (total >= kMaxSprites) break;
                    SpriteGpu gpu;
                    gpu.positionRotation = glm::vec4(s.position - camera, s.rotation);
                    gpu.sizeCustom = glm::vec4(s.size, s.custom, 0.0f);
                    gpu.uv = s.uv;
                    gpu.axis = glm::vec4(s.axis, 0.0f);
                    gpu.data = glm::uvec4(s.color, uint32_t(texture & 0xFFFFFFFFu), uint32_t(texture >> 32), uint32_t(batch->facing) | (flags << 8));
                    out.push_back(gpu);
                    ++total;
                }
                if (batch->sort == 1 && out.size() - begin > 1) {
                    // Relative to the camera already : the distance is the length. Far first, so that the blending is right.
                    std::stable_sort(out.begin() + long(begin), out.end(), [](const SpriteGpu& a, const SpriteGpu& b) {
                        return glm::dot(glm::vec3(a.positionRotation), glm::vec3(a.positionRotation)) > glm::dot(glm::vec3(b.positionRotation), glm::vec3(b.positionRotation));
                    });
                }
            }
            ranges[g].count = total - ranges[g].first;
        }
    }

    void SpriteRenderer::Init(IRenderContext* renderer)
    {
        m_Renderer = renderer;

        const Filesystem::Path directory = Core::GetEngine().GetFileManager()->GetEngineResRoot() / "shaders/sprites";
        std::shared_ptr<Shader> shader = Shader::Create(directory / "sprite.vert", directory / "sprite.frag", Filesystem::Path(""));
        if (!shader) {
            DEBUG_ERROR("SpriteRenderer : shaders/sprites/sprite is missing or does not build, the sprites of the plugins will not be drawn.");
            return;
        }

        for (uint32_t g = 0; g < kGroups; ++g) {
            const uint32_t blend = g / 2;
            const bool depthTest = (g % 2) == 0;

            PipelineSpecifications specs;
            specs.shader = shader;
            specs.depthTest = depthTest;
            specs.depthWrite = blend == 0 && depthTest;
            specs.cullMode = CullMode::None;
            specs.vertexLayout = {};
            specs.blending = blend != 0;
            if (blend == 1) { specs.srcBlend = BlendFactor::SrcAlpha; specs.dstBlend = BlendFactor::OneMinusSrcAlpha; }
            else if (blend == 2) { specs.srcBlend = BlendFactor::SrcAlpha; specs.dstBlend = BlendFactor::One; }
            else if (blend == 3) { specs.srcBlend = BlendFactor::One; specs.dstBlend = BlendFactor::OneMinusSrcAlpha; }
            specs.debugName = kPassNames[g];
            std::shared_ptr<Pipeline> pipeline = renderer->GetOrAddPipeline(specs);

            if (!m_Material)
                m_Material = Material::Create(shader, pipeline, false, Opacity::Translucent);

            std::shared_ptr<RenderPass> pass = std::make_shared<RenderPass>();
            pass->target = renderer->GetViewportFramebuffer();
            pass->clearColor = false;
            pass->clearDepth = false;
            pass->overridePipeline = true;
            pass->customPipeline = pipeline;
            pass->allowCulling = false;
            pass->enabled = false;                          // until there is something to draw
            pass->customUniforms["viewportSize"] = glm::vec2(1.0f);
            pass->customUniforms["firstInstance"] = 0;
            m_Passes[g] = pass;

            renderer->AddRenderPass(pass, kPassNames[g], {"ForwardPass"});
        }
    }

    void SpriteRenderer::Sync(const RenderSnapshot& snapshot)
    {
        if (!m_Renderer || !m_Material)
            return;

        Range ranges[kGroups];
        Build(snapshot, m_Scratch, ranges);
        const uint32_t total = uint32_t(m_Scratch.size());
        m_Drawn = total;

        if (total > 0) {
            if (!m_Instances || total > m_Capacity) {
                m_Capacity = std::max(kMinCapacity, m_Capacity);
                while (m_Capacity < total)
                    m_Capacity *= 2;
                m_Instances = StorageBuffer::Create(m_Capacity * uint32_t(sizeof(SpriteGpu)));
            }
            // The buffer is uploaded whole : a size that is not the allocation's would reallocate it
            m_Scratch.resize(m_Capacity);
            m_Instances->SetData(m_Scratch.data(), m_Capacity * uint32_t(sizeof(SpriteGpu)));
            m_Instances->Bind(kInstancesBinding);
        }

        const glm::vec2 viewport(float(std::max(snapshot.viewportWidth, 1)), float(std::max(snapshot.viewportHeight, 1)));
        for (uint32_t g = 0; g < kGroups; ++g) {
            const std::shared_ptr<RenderPass>& pass = m_Passes[g];
            const bool active = ranges[g].count > 0;
            pass->enabled = active;
            if (!active)
                continue;
            pass->customUniforms["viewportSize"] = viewport;
            pass->customUniforms["firstInstance"] = static_cast<int>(ranges[g].first);

            // A world reload clears the draw list of every pass : put the command back
            if (pass->drawList.empty()) {
                DrawCommand command{};
                command.commandID = CommandIdOf(g);
                command.fullscreenTri = true;
                command.proceduralVertexCount = 6;
                command.instanceCount = 0;
                command.bindCameraState = true;
                command.material = m_Material;
                m_Renderer->AddOrUpdateCommands({command}, {kPassNames[g]}, false);
            }
            for (DrawCommand& command : pass->drawList)
                command.instanceCount = ranges[g].count;
        }
    }
}

#include "billboard_renderer.hpp"

#include <algorithm>
#include <cmath>

#include "engine/world/engine.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/core/diagnostics/logger.hpp"

#include "engine/renderer/features/billboards/billboard_atlas.hpp"
#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/render_pass.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"

namespace Shard::Engine::Rendering {

    namespace {
        // Where the shader reads the instances (see billboard.vert) : above every binding the other shaders use
        constexpr uint32_t kInstancesBinding = 24;
        constexpr uint32_t kMinCapacity = 64;

        constexpr const char* kPassNames[2] = {"BillboardPass", "BillboardOverlayPass"};

        uint64_t CommandIdOf(int layer)
        {
            constexpr uint64_t kBillboardComponentID = 0x42494C4C;     // "BILL"
            return MakeCommandID(0, kBillboardComponentID, uint64_t(layer));
        }

        bool IsDrawn(const BillboardProxy& proxy, bool showEditor)
        {
            return proxy.size > 0.0f && ((proxy.flags & BillboardFlag::EditorOnly) == 0 || showEditor);
        }

        struct Sortable {
            BillboardInstance instance;
            float distanceSquared;
        };
    }

    BillboardRenderer::BillboardRenderer() = default;
    BillboardRenderer::~BillboardRenderer() = default;

    void BillboardRenderer::BuildInstances(const RenderSnapshot& snapshot, bool showEditor, std::vector<BillboardInstance>& out, uint32_t& depthTested)
    {
        out.clear();
        depthTested = 0;

        if (!snapshot.camera.valid || snapshot.billboards.empty())
            return;

        const BillboardAtlas& atlas = BillboardAtlas::Get();
        const glm::dvec3 cameraPosition(snapshot.camera.position);

        std::vector<Sortable> layers[2];
        for (const BillboardProxy& proxy : snapshot.billboards)
        {
            if (!IsDrawn(proxy, showEditor))
                continue;

            // 64-bit positions are brought to the camera before they become floats : far from the origin the difference is exact
            const glm::dvec3 relative = proxy.position - cameraPosition;

            Sortable item;
            item.instance.positionSize = glm::vec4(glm::vec3(relative), proxy.size);
            item.instance.tint = proxy.tint;
            item.instance.uv = atlas.GetUV(proxy.sprite);
            item.instance.params = glm::uvec4(static_cast<uint32_t>(proxy.facing), proxy.flags, 0u, 0u);
            item.distanceSquared = float(glm::dot(relative, relative));

            layers[(proxy.flags & BillboardFlag::DepthTest) ? 0 : 1].push_back(item);
        }

        for (int layer = 0; layer < 2; layer++)
        {
            // Blended, not depth written : the farthest first. Stable, so that equal distances do not flicker.
            std::stable_sort(layers[layer].begin(), layers[layer].end(), [](const Sortable& a, const Sortable& b) { return a.distanceSquared > b.distanceSquared; });
            for (const Sortable& item : layers[layer])
                out.push_back(item.instance);
            if (layer == 0)
                depthTested = static_cast<uint32_t>(layers[0].size());
        }
    }

    BillboardPick BillboardRenderer::Pick(const RenderSnapshot& snapshot, bool showEditor, float mouseX, float mouseY, float viewportWidth, float viewportHeight, float slackPixels)
    {
        BillboardPick best;
        const CameraProxy& camera = snapshot.camera;
        if (!camera.valid || viewportWidth <= 0.0f || viewportHeight <= 0.0f)
            return best;

        float bestDepth = 0.0f;
        for (const BillboardProxy& proxy : snapshot.billboards)
        {
            if (!IsDrawn(proxy, showEditor) || !(proxy.flags & BillboardFlag::Pickable) || proxy.objectId < 0)
                continue;

            const glm::vec3 relative = glm::vec3(proxy.position - glm::dvec3(camera.position));
            const glm::vec4 clip = camera.viewProjection * glm::vec4(glm::vec3(proxy.position), 1.0f);
            if (clip.w <= 0.0f)
                continue;

            const float x = (clip.x / clip.w * 0.5f + 0.5f) * viewportWidth;
            const float y = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * viewportHeight;

            // Half the quad's side in pixels : given in pixels already, or a size in meters seen from this distance
            float half = proxy.size * 0.5f;
            if (!(proxy.flags & BillboardFlag::ScreenSpaceSize))
                half = proxy.size * 0.5f * camera.projection[1][1] / clip.w * viewportHeight * 0.5f;

            if (std::abs(mouseX - x) > half + slackPixels || std::abs(mouseY - y) > half + slackPixels)
                continue;

            if (!best.hit || clip.w < bestDepth)
            {
                best.hit = true;
                best.objectId = proxy.objectId;
                best.key = proxy.key;
                best.distance = glm::length(relative);
                bestDepth = clip.w;
            }
        }
        return best;
    }

    void BillboardRenderer::Init(IRenderContext* renderer)
    {
        m_Renderer = renderer;

        // The spritesheet's icons are files read at runtime (a sprite is then BillboardAtlas::Find("name"))
        BillboardAtlas::Get().LoadFolder("editor_resources/billboard_icons");

        std::shared_ptr<Shader> shader = Core::GetEngine().GetResourcesManager()->Get<Shader>(Core::Resources::AssetKind::Shader, "shaders/billboard/billboard");
        if (!shader)
        {
            DEBUG_ERROR("BillboardRenderer : shaders/billboard/billboard is missing, the billboards will not be drawn.");
            return;
        }

        for (int layer = 0; layer < 2; layer++)
        {
            PipelineSpecifications specs;
            specs.shader = shader;
            specs.depthTest = layer == 0;
            specs.depthWrite = false;
            specs.blending = true;
            specs.cullMode = CullMode::None;
            specs.vertexLayout = {};
            specs.debugName = layer == 0 ? "BillboardPipeline" : "BillboardOverlayPipeline";
            std::shared_ptr<Pipeline> pipeline = renderer->GetOrAddPipeline(specs);

            // Translucent : the material overwrites the pipeline's blend state from its opacity
            m_Materials[layer] = Material::Create(shader, pipeline, false, Opacity::Translucent);

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
            m_Layers[layer].pass = pass;

            renderer->AddRenderPass(pass, kPassNames[layer], {"ForwardPass"});
        }
    }

    void BillboardRenderer::EnsureCommand(Layer& layer, const char* passName, uint64_t commandId)
    {
        // A world reload clears the draw list of every pass : put the command back
        if (!layer.pass->drawList.empty())
            return;

        const int index = &layer == &m_Layers[0] ? 0 : 1;

        DrawCommand command{};
        command.commandID = commandId;
        command.fullscreenTri = true;
        command.proceduralVertexCount = 6;
        command.instanceCount = 0;
        command.bindCameraState = true;
        command.material = m_Materials[index];
        m_Renderer->AddOrUpdateCommands({command}, {passName}, false);
    }

    void BillboardRenderer::Sync(const RenderSnapshot& snapshot)
    {
        if (!m_Renderer || !m_Materials[0])
            return;

        BillboardAtlas& atlas = BillboardAtlas::Get();
        atlas.Rebuild();

        BuildInstances(snapshot, m_ShowEditor.load(std::memory_order_relaxed), m_Scratch, m_Layers[0].count);

        const uint32_t total = static_cast<uint32_t>(m_Scratch.size());
        m_Layers[0].first = 0;
        m_Layers[1].first = m_Layers[0].count;
        m_Layers[1].count = total - m_Layers[0].count;
        m_Drawn.store(total, std::memory_order_relaxed);

        if (total > 0)
        {
            if (!m_Instances || total > m_Capacity)
            {
                m_Capacity = std::max(kMinCapacity, m_Capacity);
                while (m_Capacity < total)
                    m_Capacity *= 2;
                m_Instances = StorageBuffer::Create(m_Capacity * uint32_t(sizeof(BillboardInstance)));
            }

            // The buffer is uploaded whole : a size that is not the allocation's would reallocate it
            m_Scratch.resize(m_Capacity);
            m_Instances->SetData(m_Scratch.data(), m_Capacity * uint32_t(sizeof(BillboardInstance)));
            m_Instances->Bind(kInstancesBinding);
        }

        const std::shared_ptr<Texture2D> atlasTexture = atlas.GetTexture();
        const glm::vec2 viewport(float(std::max(snapshot.viewportWidth, 1)), float(std::max(snapshot.viewportHeight, 1)));

        for (int i = 0; i < 2; i++)
        {
            Layer& layer = m_Layers[i];
            const bool active = layer.count > 0 && atlasTexture;
            layer.pass->enabled = active;
            if (!active)
                continue;

            layer.pass->customSamplers["uAtlas"] = atlasTexture->GetHandle();
            layer.pass->customUniforms["viewportSize"] = viewport;
            layer.pass->customUniforms["firstInstance"] = static_cast<int>(layer.first);

            EnsureCommand(layer, kPassNames[i], CommandIdOf(i));
            for (DrawCommand& command : layer.pass->drawList)
                command.instanceCount = layer.count;
        }
    }
}

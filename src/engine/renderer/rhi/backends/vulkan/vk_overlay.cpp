#include "vk_api.hpp"

#include <algorithm>
#include <cstring>

#include "vk_pipeline.hpp"
#include "vk_shader.hpp"

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/render_snapshot.hpp"
#include "engine/renderer/rhi/render_thread.hpp"
#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/world/engine.hpp"

// The 2D overlay of shard.render, on Vulkan : what a plugin recorded (a UI's rectangles and glyphs) drawn over the picture with premultiplied alpha,
// in pixels from the top left of the target. Same result as gl_overlay.cpp, made with the machinery of the renderer instead of a program of its
// own : shaders/overlay/overlay.{vert,frag} read the vertices and the indices of the whole overlay from two storage buffers, so a draw is a
// procedural draw (no vertex buffer, no mesh) and the geometries need no buffers of their own. They are packed again every frame ; a UI is a few
// thousand vertices.

namespace Shard::Engine::Rendering {

    namespace {

        constexpr uint32_t kVerticesBinding = 26;
        constexpr uint32_t kIndicesBinding = 27;
        constexpr uint32_t kMinCapacity = 4096;

        struct Rect {
            int x = 0, y = 0, w = 0, h = 0;
            Rect Intersect(const Rect& o) const
            {
                const int x0 = std::max(x, o.x), y0 = std::max(y, o.y);
                const int x1 = std::min(x + w, o.x + o.w), y1 = std::min(y + h, o.y + o.h);
                return Rect{ x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0) };
            }
        };

        /// Writes `data` to a storage buffer that is at least that big. The buffer is uploaded whole (a size that is not the allocation's would
        /// reallocate it), so it only grows, by doubling.
        template<typename T>
        void Upload(std::shared_ptr<StorageBuffer>& buffer, uint32_t& capacity, const std::vector<T>& data)
        {
            const uint32_t bytes = uint32_t(data.size() * sizeof(T));
            if (!buffer || bytes > capacity) {
                capacity = std::max(kMinCapacity, capacity);
                while (capacity < bytes)
                    capacity *= 2;
                buffer = StorageBuffer::Create(capacity);
            }
            std::vector<uint8_t> padded(capacity, 0);
            if (bytes > 0)
                std::memcpy(padded.data(), data.data(), bytes);
            buffer->SetData(padded.data(), capacity);
        }
    }

    void VKRendererAPI::DrawOverlay(const OverlayProxy& overlay, uint32_t width, uint32_t height)
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        if (overlay.commands.empty() || width == 0 || height == 0 || !HasTarget() || m_Overlay.failed)
            return;

        OverlayResources& resources = m_Overlay;
        if (!resources.pipeline) {
            const Filesystem::Path directory = Core::GetEngine().GetFileManager()->GetEngineResRoot() / "shaders/overlay";
            resources.shader = Shader::Create(directory / "overlay.vert", directory / "overlay.frag", Filesystem::Path(""));
            if (!resources.shader) {
                DEBUG_ERROR("The overlay shader (shaders/overlay/overlay) is missing or does not build : the overlays of the plugins will not be drawn.");
                resources.failed = true;
                return;
            }

            PipelineSpecifications specs;
            specs.shader = resources.shader;
            specs.depthTest = false;
            specs.depthWrite = false;
            specs.cullMode = CullMode::None;
            specs.vertexLayout = {};
            specs.blending = true;
            specs.srcBlend = BlendFactor::One;
            specs.dstBlend = BlendFactor::OneMinusSrcAlpha;
            specs.debugName = "OverlayPipeline";
            resources.pipeline = std::make_shared<VKPipeline>(specs);
        }

        // ---- The vertices and the indices of the whole overlay, in two buffers ----
        struct Placement { uint32_t baseVertex = 0, firstIndex = 0; };
        std::unordered_map<const OverlayGeometry*, Placement> placed;
        std::vector<OverlayVertex> vertices = overlay.inlineVertices;       // Kind::Triangles : its indices are the proxy's, from 0
        std::vector<uint32_t> indices = overlay.inlineIndices;

        for (const OverlayCommand& c : overlay.commands) {
            if (c.kind != OverlayCommand::Kind::Geometry || !c.geometry || c.geometry->indices.empty() || placed.count(c.geometry.get()))
                continue;
            placed[c.geometry.get()] = Placement{ uint32_t(vertices.size()), uint32_t(indices.size()) };
            vertices.insert(vertices.end(), c.geometry->vertices.begin(), c.geometry->vertices.end());
            indices.insert(indices.end(), c.geometry->indices.begin(), c.geometry->indices.end());
        }

        if (!indices.empty()) {
            // The upload goes in the command stream before the draws and ends the render pass in progress ; the target is loaded by the next one
            Upload(resources.vertices, resources.vertexCapacity, vertices);
            Upload(resources.indices, resources.indexCapacity, indices);
            resources.vertices->Bind(kVerticesBinding);
            resources.indices->Bind(kIndicesBinding);
        }

        // ---- State ----
        const bool hadViewport = m_ViewportSet;
        const VkViewport previousViewport = m_Viewport;
        SetViewport(0, 0, width, height);

        std::shared_ptr<Shader>& shader = resources.shader;
        const glm::mat4 projection = glm::mat4(2.0f / float(width), 0, 0, 0,
                                               0, -2.0f / float(height), 0, 0,
                                               0, 0, 1, 0,
                                               -1, 1, 0, 1);
        shader->SetMat4("uProjection", projection);

        glm::mat4 transform(1.0f);
        const Rect full{ 0, 0, int(width), int(height) };
        Rect user = full;                           // set_scissor
        Rect clip = full;                           // set_clip_mask, as the bounding rectangle of the mask
        bool visible = true;

        auto applyScissor = [&] {
            const Rect r = user.Intersect(clip).Intersect(full);
            visible = r.w > 0 && r.h > 0;
            // The images are stored bottom row first, like OpenGL's (see the viewport)
            const VkRect2D scissor{ { r.x, int(height) - (r.y + r.h) }, { uint32_t(r.w), uint32_t(r.h) } };
            SetScissor(&scissor);
        };
        applyScissor();

        auto draw = [&](uint32_t baseVertex, uint32_t firstIndex, uint32_t count, const glm::vec2& translation, const std::shared_ptr<Texture2D>& texture) {
            if (!visible || count == 0)
                return;
            const uint64_t handle = texture ? texture->GetBindlessHandle() : 0;
            shader->SetMat4("uTransform", transform);
            shader->SetVec2("uTranslation", translation);
            shader->SetInt("uBaseVertex", int(baseVertex));
            shader->SetInt("uFirstIndex", int(firstIndex));
            shader->SetUVec2("uTexture", uint32_t(handle & 0xFFFFFFFFu), uint32_t(handle >> 32));

            DrawCommand command{};
            command.fullscreenTri = true;
            command.proceduralVertexCount = count;
            command.instanceCount = 1;
            IssueDraw(command, resources.pipeline, true, false);
        };

        for (const OverlayCommand& c : overlay.commands) {
            switch (c.kind) {
                case OverlayCommand::Kind::Transform:
                    transform = c.transform;
                    break;

                case OverlayCommand::Kind::Scissor:
                    user = c.enabled ? Rect{ c.scissor[0], c.scissor[1], c.scissor[2], c.scissor[3] } : full;
                    applyScissor();
                    break;

                case OverlayCommand::Kind::ClipMask: {
                    // SHARD_CLIP_DISABLE 0, SET 1, SET_INVERSE 2, INTERSECT 3
                    if (c.operation == 0 || c.operation == 2 || !c.geometry || c.geometry->vertices.empty()) {
                        clip = full;                // no mask, or everything but a shape : not representable by a rectangle
                    } else {
                        float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
                        for (const OverlayVertex& v : c.geometry->vertices) {
                            const glm::vec4 p = transform * glm::vec4(v.x + c.translation.x, v.y + c.translation.y, 0.0f, 1.0f);
                            lo[0] = std::min(lo[0], p.x); lo[1] = std::min(lo[1], p.y);
                            hi[0] = std::max(hi[0], p.x); hi[1] = std::max(hi[1], p.y);
                        }
                        const Rect mask{ int(lo[0]), int(lo[1]), int(hi[0] - lo[0] + 1.0f), int(hi[1] - lo[1] + 1.0f) };
                        clip = c.operation == 3 ? clip.Intersect(mask) : mask;
                    }
                    applyScissor();
                    break;
                }

                case OverlayCommand::Kind::Clear: {
                    // The whole target, whatever the scissor is
                    if (!m_RenderingActive)
                        BeginRendering();
                    if (!m_RenderingActive)
                        break;
                    VkClearAttachment attachment{};
                    attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    attachment.colorAttachment = 0;
                    attachment.clearValue.color = {{ c.color.r, c.color.g, c.color.b, c.color.a }};
                    VkClearRect area{};
                    area.rect = { { 0, 0 }, { m_ActiveTarget.width, m_ActiveTarget.height } };
                    area.layerCount = 1;
                    vkCmdClearAttachments(VK::Context::Get().Cmd(), 1, &attachment, 1, &area);
                    break;
                }

                case OverlayCommand::Kind::Geometry: {
                    if (!c.geometry)
                        break;
                    const auto it = placed.find(c.geometry.get());
                    if (it == placed.end())
                        break;
                    draw(it->second.baseVertex, it->second.firstIndex, uint32_t(c.geometry->indices.size()), c.translation, c.texture);
                    break;
                }

                case OverlayCommand::Kind::Triangles:
                    if (c.count == 0 || c.first + c.count > overlay.inlineIndices.size())
                        break;
                    draw(0, c.first, c.count, glm::vec2(0.0f), c.texture);
                    break;
            }
        }

        SetScissor(nullptr);
        m_ViewportSet = hadViewport;
        m_Viewport = previousViewport;
        InvalidateStateCache();
    }
}

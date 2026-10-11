#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "engine/renderer/rhi/render_snapshot.hpp"

namespace Shard::Engine::Rendering {

    class IRenderContext;
    class Material;
    struct RenderPass;
    class StorageBuffer;

    /// What a billboard is on the GPU : 64 bytes, std430 (see shaders/billboard/billboard.vert)
    struct BillboardInstance {
        glm::vec4 positionSize;     // xyz : relative to the camera, w : size
        glm::vec4 tint;
        glm::vec4 uv;               // u0 v0 u1 v1 in the atlas
        glm::uvec4 params;          // x : facing, y : flags, zw : unused
    };
    static_assert(sizeof(BillboardInstance) == 64, "BillboardInstance is mirrored by the shader");

    /// The billboard under the cursor
    struct BillboardPick {
        bool hit = false;
        int objectId = -1;
        uint64_t key = 0;
        float distance = 0.0f;      // from the camera, in meters : comparable with the distance of a ray hit
    };

    /// @brief Draws the snapshot's billboards : every quad of every kind (editor icons, sprites, bone / IK handles...) in one instanced
    /// draw call per layer, the vertex shader building the quads from gl_VertexID. Two layers : the billboards the geometry hides,
    /// and the ones drawn over everything.
    /// @note Graphics thread only, except SetShowEditorBillboards. It reads nothing but the snapshot.
    class BillboardRenderer {
        public:
            BillboardRenderer();
            ~BillboardRenderer();

            void Init(IRenderContext* renderer);

            /// Brings the passes in line with the snapshot : uploads the instances and sets how many each layer draws. Once per frame.
            void Sync(const RenderSnapshot& snapshot);

            /// The billboards flagged EditorOnly (light and camera icons...) are drawn only while this is on. The editor turns it on.
            void SetShowEditorBillboards(bool show) { m_ShowEditor.store(show, std::memory_order_relaxed); }
            bool GetShowEditorBillboards() const { return m_ShowEditor.load(std::memory_order_relaxed); }

            /// How many billboards the last frame drew
            uint32_t GetDrawnCount() const { return m_Drawn.load(std::memory_order_relaxed); }

            /// Turns the snapshot's billboards into instances : the ones with the depth test first (`depthTested` receives how many),
            /// each layer sorted from the farthest to the nearest so that the blending is right. Pure CPU.
            static void BuildInstances(const RenderSnapshot& snapshot, bool showEditor, std::vector<BillboardInstance>& out, uint32_t& depthTested);

            /// The nearest pickable billboard whose quad contains the pixel (`mouseX`, `mouseY` from the top left of the viewport).
            /// `slackPixels` makes the small ones easier to hit. Pure CPU.
            static BillboardPick Pick(const RenderSnapshot& snapshot, bool showEditor, float mouseX, float mouseY, float viewportWidth, float viewportHeight, float slackPixels = 3.0f);

        private:
            struct Layer {
                std::shared_ptr<RenderPass> pass;
                uint32_t first = 0;
                uint32_t count = 0;
            };

            void EnsureCommand(Layer& layer, const char* passName, uint64_t commandId);

            IRenderContext* m_Renderer = nullptr;
            std::shared_ptr<Material> m_Materials[2];
            Layer m_Layers[2];                                  // 0 : depth tested, 1 : over everything
            std::shared_ptr<StorageBuffer> m_Instances;
            uint32_t m_Capacity = 0;                            // in instances
            std::vector<BillboardInstance> m_Scratch;
            uint64_t m_AtlasVersion = 0;
            std::atomic<bool> m_ShowEditor{false};
            std::atomic<uint32_t> m_Drawn{0};
    };
}

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

    struct BillboardInstance {
        glm::vec4 positionSize;
        glm::vec4 tint;
        glm::vec4 uv;
        glm::uvec4 params;
    };
    static_assert(sizeof(BillboardInstance) == 64, "BillboardInstance is mirrored by the shader");

    struct BillboardPick {
        bool hit = false;
        int objectId = -1;
        uint64_t key = 0;
        float distance = 0.0f;
    };

    class BillboardRenderer {
        public:
            BillboardRenderer();
            ~BillboardRenderer();

            void Init(IRenderContext* renderer);

            void Sync(const RenderSnapshot& snapshot);

            void SetShowEditorBillboards(bool show) { m_ShowEditor.store(show, std::memory_order_relaxed); }
            bool GetShowEditorBillboards() const { return m_ShowEditor.load(std::memory_order_relaxed); }

            uint32_t GetDrawnCount() const { return m_Drawn.load(std::memory_order_relaxed); }

            static void BuildInstances(const RenderSnapshot& snapshot, bool showEditor, std::vector<BillboardInstance>& out, uint32_t& depthTested);

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
            Layer m_Layers[2];
            std::shared_ptr<StorageBuffer> m_Instances;
            uint32_t m_Capacity = 0;
            std::vector<BillboardInstance> m_Scratch;
            uint64_t m_AtlasVersion = 0;
            std::atomic<bool> m_ShowEditor{false};
            std::atomic<uint32_t> m_Drawn{0};
    };
}

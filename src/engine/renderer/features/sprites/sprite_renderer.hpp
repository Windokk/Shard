#pragma once

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

    // 80 bytes, std430, matches shaders/sprites/sprite.vert.
    struct SpriteGpu {
        glm::vec4 positionRotation;
        glm::vec4 sizeCustom;
        glm::vec4 uv;
        glm::vec4 axis;
        glm::uvec4 data;
    };
    static_assert(sizeof(SpriteGpu) == 80, "SpriteGpu is mirrored by the shader");

    class SpriteRenderer {
        public:
            static constexpr uint32_t kGroups = 8;
            struct Range { uint32_t first = 0, count = 0; };

            SpriteRenderer();
            ~SpriteRenderer();

            void Init(IRenderContext* renderer);

            void Sync(const RenderSnapshot& snapshot);

            uint32_t GetDrawnCount() const { return m_Drawn; }

            static void Build(const RenderSnapshot& snapshot, std::vector<SpriteGpu>& out, Range (&ranges)[kGroups]);

        private:
            IRenderContext* m_Renderer = nullptr;
            std::shared_ptr<Material> m_Material;
            std::shared_ptr<RenderPass> m_Passes[kGroups];
            std::shared_ptr<StorageBuffer> m_Instances;
            uint32_t m_Capacity = 0;
            std::vector<SpriteGpu> m_Scratch;
            uint32_t m_Drawn = 0;
    };
}

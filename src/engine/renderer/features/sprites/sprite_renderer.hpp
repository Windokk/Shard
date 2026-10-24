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

    /// What a sprite is on the GPU : 80 bytes, std430 (see shaders/sprites/sprite.vert)
    struct SpriteGpu {
        glm::vec4 positionRotation;     // xyz : relative to the camera, w : rotation
        glm::vec4 sizeCustom;           // xy : size, z : custom
        glm::vec4 uv;
        glm::vec4 axis;
        glm::uvec4 data;                // x : colour, yz : the texture's bindless handle, w : facing | flags << 8
    };
    static_assert(sizeof(SpriteGpu) == 80, "SpriteGpu is mirrored by the shader");

    /// @brief Draws the snapshot's sprite batches (shard.render's particles and flares) : one instanced draw of 6 vertices per sprite, a pass per blend
    /// mode and per depth setting, the quads built by the vertex shader.
    /// @note Graphics thread only. It reads nothing but the snapshot. OpenGL with bindless textures.
    class SpriteRenderer {
        public:
            static constexpr uint32_t kGroups = 8;
            struct Range { uint32_t first = 0, count = 0; };

            SpriteRenderer();
            ~SpriteRenderer();

            void Init(IRenderContext* renderer);

            /// Brings the passes in line with the snapshot : uploads the sprites and sets how many each pass draws. Once per frame.
            void Sync(const RenderSnapshot& snapshot);

            /// How many sprites the last frame drew
            uint32_t GetDrawnCount() const { return m_Drawn; }

            /// Turns the snapshot's batches into sprites relative to the camera, grouped by the pass that draws them (`ranges[group]`) and sorted far to
            /// near where a batch asks for it. Pure CPU.
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

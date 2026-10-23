#pragma once

#include "engine/renderer/rhi/resources/texture/texture.hpp"

namespace Shard::Engine::Rendering {

    /// @brief Layers of the same size and format behind one texture : a shader reads all of them through ONE sampler unit (a
    /// sampler2DArray, the layer is a coordinate), which is what lets lit.frag read every cascade or every spot light's shadow map
    /// without one unit each. The layers are drawn into one at a time (see Framebuffer::AttachDepthLayer).
    class Texture2DArray
    {
        public:

            virtual ~Texture2DArray() = default;

            virtual void Bind(uint32_t slot = 0) const = 0;

            uint32_t GetWidth() const { return m_Specifications.width; }
            uint32_t GetHeight() const { return m_Specifications.height; }
            uint32_t GetLayerCount() const { return m_Layers; }

            const TextureSpecifications& GetSpecification() const { return m_Specifications; }

            virtual uint32_t GetHandle() const = 0;

            virtual bool IsValid() const = 0;

            /// `layers` textures with nothing in them yet (a depth one holds the far depth), one level each
            static std::shared_ptr<Texture2DArray> Create(const TextureSpecifications& specs, uint32_t layers);

        protected:
            TextureSpecifications m_Specifications;
            uint32_t m_Layers = 0;
    };
}

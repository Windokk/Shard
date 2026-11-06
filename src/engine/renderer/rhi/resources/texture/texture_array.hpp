#pragma once

#include "engine/renderer/rhi/resources/texture/texture.hpp"

namespace Shard::Engine::Rendering {

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

            static std::shared_ptr<Texture2DArray> Create(const TextureSpecifications& specs, uint32_t layers);

        protected:
            TextureSpecifications m_Specifications;
            uint32_t m_Layers = 0;
    };
}

#pragma once

#include "engine/renderer/rhi/resources/texture/texture_array.hpp"

namespace Shard::Engine::Rendering{

    class GLTexture2DArray : public Texture2DArray{
        public:

            GLTexture2DArray(const TextureSpecifications& specs, uint32_t layers);

            void Bind(uint32_t slot = 0) const override;

            bool IsValid() const override { return m_ID != 0; }

            ~GLTexture2DArray() override;

            uint32_t GetHandle() const override { return m_ID; }

        private:
            uint32_t m_ID = 0;
            int64_t m_GpuBytes = 0;
    };
}

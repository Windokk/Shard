#pragma once

#include "engine/renderer/rhi/resources/texture/texture_array.hpp"

#include "vk_image.hpp"

namespace Shard::Engine::Rendering {

    class VKTexture2DArray final : public Texture2DArray
    {
        public:

            VKTexture2DArray(const TextureSpecifications& specs, uint32_t layers);
            ~VKTexture2DArray() override;

            void Bind(uint32_t slot = 0) const override;
            uint32_t GetHandle() const override { return m_Resource.GetId(); }
            bool IsValid() const override { return m_Resource.image.IsValid(); }

            VK::TextureResource& Resource() { return m_Resource; }

        private:

            mutable VK::TextureResource m_Resource;
            int64_t m_GpuBytes = 0;
    };

}

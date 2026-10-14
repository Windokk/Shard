#pragma once

#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/resources/texture/texture_compression.hpp"

#include "vk_image.hpp"

namespace Shard::Engine::Rendering {

    class VKTexture2D final : public Texture2D
    {
        public:

            VKTexture2D(TextureSpecifications& specs, const void* data);

            /// A texture whose blocks are already encoded (BC4 / BC5 / BC7), all mip levels included : uploaded as they are
            VKTexture2D(const TextureSpecifications& specs, const CompressedImage& image, uint32_t firstMip = 0);

            ~VKTexture2D() override;

            void Bind(uint32_t slot = 0) const override;
            void BindImage(uint32_t unit, TextureAccess access, uint32_t world = 0) const override;
            void ReadPixels(void* outData, size_t bufferSize, uint32_t world = 0) const override;

            uint32_t GetHandle() const override { return m_Resource.GetId(); }

            /// The place of the texture in the array of bindless textures (what the shaders read as the first of the two uints of a handle)
            uint64_t GetBindlessHandle() const override { return m_Resource.image.IsValid() ? m_Resource.GetId() : 0; }

            bool IsValid() const override { return m_Resource.image.IsValid(); }
            bool SwapContents(Texture2D& other) override;

            VK::TextureResource& Resource() { return m_Resource; }

        private:

            void CreateImage(VkFormat format, uint32_t mips, bool storage, const std::string& name);

            mutable VK::TextureResource m_Resource;
            int64_t m_GpuBytes = 0;
    };

    namespace VK {
        /// The texels of the GPU format, made of what the caller of Texture2D::Create gave (an RGB texture gets its fourth channel, float pixel data becomes half floats...)
        std::vector<uint8_t> ConvertSourcePixels(const TextureSpecifications& specs, const void* data, VkFormat format, uint32_t width, uint32_t height);
        bool SupportsStorage(VkFormat format);
        bool SupportsColorAttachment(VkFormat format);
    }

    /// float -> IEEE half, for the pixel data that goes to a 16 bit float texture
    uint16_t FloatToHalf(float value);
    float HalfToFloat(uint16_t value);

}

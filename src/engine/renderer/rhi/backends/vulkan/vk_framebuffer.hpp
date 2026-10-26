#pragma once

#include <memory>

#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"

#include "vk_api.hpp"
#include "vk_image.hpp"

namespace Shard::Engine::Rendering {

    /// @brief A framebuffer : its attachments are images of their own (the ones a shader samples later), made again when it is resized, keeping the
    /// numbers they go by. A multisampled one draws into multisampled images and has single sample images they are resolved into.
    class VKFramebuffer final : public Framebuffer
    {
        public:

            explicit VKFramebuffer(const FramebufferSpecifications& specs);
            ~VKFramebuffer();

            void Destroy() override;

            void Bind() override;
            void Unbind() override;

            void ResolveMultisampled() override;
            void Resize(uint32_t width, uint32_t height) override;
            void SetSamplesCount(uint32_t samples) override;

            void AttachCubemapArray(uint32_t texture) override;
            void DetachCubemapArray() override;

            void AttachDepthLayer(uint32_t texture, uint32_t layer) override;

            bool HasDepthTarget() const override { return m_Specifications.hasDepth || m_AttachedLayerTexture != 0; }

            void CopyFrom(std::shared_ptr<Framebuffer> src) override;
            void CopyDepthFrom(std::shared_ptr<Framebuffer> src) override;
            void BlitToScreen(uint32_t screenWidth, uint32_t screenHeight) override;

            bool ReadPixelsRGBA8(std::vector<uint8_t>& outPixels) override;
            bool ReadPixelsRGBAFloat(std::vector<float>& outPixels) override;
            void UploadColorRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const uint8_t* rgba) override;
            void BlitColorTo(Framebuffer& dst, uint32_t dstX, uint32_t dstY, uint32_t dstWidth, uint32_t dstHeight) override;

            uint32_t GetColorAttachment() const override { return m_Color ? m_Color->GetId() : 0; }
            uint32_t GetColorAttachmentAt(uint32_t index) const override
            {
                if (index == 0)
                    return GetColorAttachment();
                return index - 1 < m_ExtraColors.size() && m_ExtraColors[index - 1] ? m_ExtraColors[index - 1]->GetId() : 0;
            }
            uint32_t GetDepthAttachment() const override { return m_Depth ? m_Depth->GetId() : 0; }
            uint32_t GetResolveColorAttachment() const override;
            uint32_t GetResolveDepthAttachment() const override;
            uint64_t GetColorAttachmentBindlessHandle() const override { return GetColorAttachment(); }

            uint32_t GetHandle() const override { return m_Handle; }
            bool IsValid() const override { return !m_Destroyed; }

            /// What the API draws into when this framebuffer is the target
            VK::RenderTarget GetRenderTarget();

            /// The image the picture ends up in (the resolved one of a multisampled framebuffer)
            VK::Image* ResolvedColorImage();

        private:

            /// The texels of the resolved colour image as they are stored (bottom row first, `outFormat`) : what the readbacks convert
            bool ReadColorTexels(std::vector<uint8_t>& outTexels, VkFormat& outFormat, uint32_t& outWidth, uint32_t& outHeight);

            void Allocate();
            void Release();
            VkSampleCountFlagBits ChooseSamples() const;

            std::unique_ptr<VK::TextureResource> m_Color;
            std::unique_ptr<VK::TextureResource> m_Depth;
            std::unique_ptr<VK::TextureResource> m_ResolveColor;
            std::unique_ptr<VK::TextureResource> m_ResolveDepth;
            std::vector<std::unique_ptr<VK::TextureResource>> m_ExtraColors;     // colour attachments 1 and on (a G-buffer)

            uint32_t m_AttachedCubemap = 0;         ///< the texture (id) that replaces the depth attachment, 0 : none
            uint32_t m_AttachedLayerTexture = 0;    ///< the array (id) one layer of which replaces the depth attachment, 0 : none
            uint32_t m_AttachedLayer = 0;
            uint32_t m_Handle = 0;
            bool m_Destroyed = false;
            int64_t m_GpuBytes = 0;
    };

}

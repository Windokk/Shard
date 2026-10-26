#pragma once

#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"

namespace Shard::Engine::Rendering{

    class GLFramebuffer : public Framebuffer{
        public:
            void CheckFBStatus();

            GLFramebuffer(const FramebufferSpecifications &spec);
            void Destroy() override;

            void Bind() override;
            void Unbind() override;

            void CopyFrom(std::shared_ptr<Framebuffer> src) override;

            void CopyDepthFrom(std::shared_ptr<Framebuffer> src) override;

            void BlitToScreen(uint32_t screenWidth, uint32_t screenHeight) override;

            bool ReadPixelsRGBA8(std::vector<uint8_t>& outPixels) override;
            bool ReadPixelsRGBAFloat(std::vector<float>& outPixels) override;
            bool ReadDepthFloat(std::vector<float>& outDepth) override;

            void UploadColorRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const uint8_t* rgba) override;

            void BlitColorTo(Framebuffer& dst, uint32_t dstX, uint32_t dstY, uint32_t dstWidth, uint32_t dstHeight) override;

            void ResolveMultisampled() override;

            void AttachCubemapArray(uint32_t texture) override;

            void DetachCubemapArray() override;

            void AttachDepthLayer(uint32_t texture, uint32_t layer) override;

            bool HasDepthTarget() const override { return m_Specifications.hasDepth || m_DepthLayerTexture != 0; }

            uint32_t GetHandle() const override;

            void Resize(uint32_t width, uint32_t height) override;

            void SetSamplesCount(uint32_t samples) override;

            bool IsValid() const override;

            uint32_t GetColorAttachment() const override;
            uint32_t GetColorAttachmentAt(uint32_t index) const override;
            uint32_t GetDepthAttachment() const override;

            uint32_t GetResolveColorAttachment() const override;
            uint32_t GetResolveDepthAttachment() const override;

            uint64_t GetColorAttachmentBindlessHandle() const override;

        private:
            // Creates (or re-creates) the non-multisampled color attachment at the given size and binds
            // it to GL_COLOR_ATTACHMENT0 - expects m_FBO to already be the bound framebuffer. Shared by
            // the constructor and Resize()'s bindless path (see the comment there).
            void CreateColorAttachment(uint32_t width, uint32_t height);

            // What GpuMemory was told this framebuffer holds, kept in line after every (re)allocation
            void UpdateGpuAccounting(bool destroyed = false);
            int64_t m_GpuBytes = 0;
            bool m_Destroyed = false;

            uint32_t m_FBO = 0;
            uint32_t m_ColorAttachment = 0;
            uint32_t m_DepthAttachment = 0;
            uint32_t m_DepthLayerTexture = 0;                   // the array whose layer stands for the depth attachment (see AttachDepthLayer)
            std::vector<uint32_t> m_ExtraColorAttachments;     // colour attachments 1 and on (a G-buffer)

            /// Creates / re-specifies the extra colour attachments at the given size and attaches them (expects m_FBO bound), then sets the draw buffers
            void CreateExtraColorAttachments(uint32_t width, uint32_t height);
            // The draw buffers of the framebuffer that is bound : `withExtras` is false for the resolve framebuffer of a multisampled one, which has no extra attachments
            void ApplyDrawBuffers(bool withExtras = true);

            uint32_t m_ResolveFBO = 0;
            uint32_t m_ResolveColorAttachment = 0;
            uint32_t m_ResolveDepthAttachment = 0;
    };

}
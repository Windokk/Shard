#pragma once

#include "engine/rendering/framebuffer/framebuffer.hpp"

namespace Shard::Engine::Rendering{

    class GLFramebuffer : public Framebuffer{
        public:
            void CheckFBStatus();

            GLFramebuffer(const FramebufferSpecifications &spec);
            void Destroy() override;

            void Bind() override;
            void Unbind() override;

            void CopyFrom(std::shared_ptr<Framebuffer> src) override;

            void BlitToScreen(uint32_t screenWidth, uint32_t screenHeight) override;

            bool ReadPixelsRGBA8(std::vector<uint8_t>& outPixels) override;

            void UploadColorRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const uint8_t* rgba) override;

            void BlitColorTo(Framebuffer& dst, uint32_t dstX, uint32_t dstY, uint32_t dstWidth, uint32_t dstHeight) override;

            void ResolveMultisampled() override;

            void AttachCubemapArray(uint32_t texture) override;

            void DetachCubemapArray() override;

            uint32_t GetHandle() const override;

            void Resize(uint32_t width, uint32_t height) override;

            bool IsValid() const override;

            uint32_t GetColorAttachment() const override;
            uint32_t GetDepthAttachment() const override;

            uint32_t GetResolveColorAttachment() const override;
            uint32_t GetResolveDepthAttachment() const override;

            uint64_t GetColorAttachmentBindlessHandle() const override;

        private:
            // Creates (or re-creates) the non-multisampled color attachment at the given size and binds
            // it to GL_COLOR_ATTACHMENT0 - expects m_FBO to already be the bound framebuffer. Shared by
            // the constructor and Resize()'s bindless path (see the comment there).
            void CreateColorAttachment(uint32_t width, uint32_t height);

            uint32_t m_FBO = 0;
            uint32_t m_ColorAttachment = 0;
            uint32_t m_DepthAttachment = 0;

            uint32_t m_ResolveFBO = 0;
            uint32_t m_ResolveColorAttachment = 0;
            uint32_t m_ResolveDepthAttachment = 0;
    };

}
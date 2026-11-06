#pragma once

#include "engine/core/color.hpp"

#include "engine/renderer/rhi/resources/texture/texture.hpp"

namespace Shard::Engine::Rendering {

    struct FramebufferSpecifications
    {
        uint32_t width = 0;
        uint32_t height = 0;

        bool multisampled = false;
        uint32_t samplesCount = 4;

        bool hasColor = true;
        bool hasDepth = true;

        std::vector<TextureSpecifications> extraColorSpecs;

        TextureSpecifications colorSpecs = []{
            TextureSpecifications s;
            s.internalFormat = TextureInternalFormat::RGBA8;
            return s;
        }();

        TextureSpecifications depthSpecs = []{
            TextureSpecifications s;
            s.internalFormat = TextureInternalFormat::Depth24;
            s.minFilter = TextureFilter::Nearest;
            s.magFilter = TextureFilter::Nearest;
            s.wrapS = TextureWrap::ClampEdge;
            s.wrapT = TextureWrap::ClampEdge;
            return s;
        }();
    };

    class Framebuffer
    {
        public:

            virtual void Destroy() = 0;

            virtual void Bind() = 0;
            virtual void Unbind() = 0;

            virtual void ResolveMultisampled() = 0;

            virtual void Resize(uint32_t width, uint32_t height) = 0;

            virtual void SetSamplesCount(uint32_t samples) = 0;

            virtual void AttachCubemapArray(uint32_t texture) = 0;

            virtual void DetachCubemapArray() = 0;

            virtual void AttachDepthLayer(uint32_t texture, uint32_t layer) = 0;

            virtual bool HasDepthTarget() const { return m_Specifications.hasDepth; }

            virtual void CopyFrom(std::shared_ptr<Framebuffer> src) = 0;

            // Same size and same multisample kind required on both.
            virtual void CopyDepthFrom(std::shared_ptr<Framebuffer> src) = 0;

            virtual void BlitToScreen(uint32_t screenWidth, uint32_t screenHeight) = 0;

            virtual bool ReadPixelsRGBA8(std::vector<uint8_t>& outPixels) = 0;

            virtual bool ReadPixelsRGBAFloat(std::vector<float>& outPixels) { (void)outPixels; return false; }

            virtual bool ReadDepthFloat(std::vector<float>& outDepth) { (void)outDepth; return false; }

            virtual void UploadColorRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const uint8_t* rgba) = 0;

            virtual void BlitColorTo(Framebuffer& dst, uint32_t dstX, uint32_t dstY, uint32_t dstWidth, uint32_t dstHeight) = 0;

            virtual uint32_t GetColorAttachmentAt(uint32_t index) const { return index == 0 ? GetColorAttachment() : 0; }

            virtual uint32_t GetColorAttachment() const = 0;
            virtual uint32_t GetDepthAttachment() const = 0;
            virtual uint32_t GetResolveColorAttachment() const = 0;
            virtual uint32_t GetResolveDepthAttachment() const = 0;

            virtual uint64_t GetColorAttachmentBindlessHandle() const = 0;

            virtual uint32_t GetHandle() const = 0;
            
            virtual bool IsValid() const = 0;

            uint32_t GetWidth() const { return m_Specifications.width; }
            uint32_t GetHeight() const { return m_Specifications.height; }

            const FramebufferSpecifications& GetSpecifications() const {return m_Specifications; }

            static std::shared_ptr<Framebuffer> Create(
                const FramebufferSpecifications& specs
            );

        protected:
            
            FramebufferSpecifications m_Specifications;
    };
}
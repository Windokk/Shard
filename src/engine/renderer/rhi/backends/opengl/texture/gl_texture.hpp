#pragma once

#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/resources/texture/texture_compression.hpp"

namespace Shard::Engine::Rendering{

    struct GLTextureSpec{
        
        uint32_t wrapModeS;
        uint32_t wrapModeT;
        uint32_t wrapModeR;

        uint32_t minFilter;
        uint32_t magFilter;

        uint32_t internalFormat;
        uint32_t format;
        
        uint32_t compareFunc;
        
        uint32_t type;

        static GLTextureSpec FromTextureSpecifications(const TextureSpecifications& spec);
    };

    class GLTexture2D : public Texture2D{

        public:

            GLTexture2D(TextureSpecifications& specs, const void* data);

            GLTexture2D(const TextureSpecifications& specs, const CompressedImage& image, uint32_t firstMip = 0);

            void Bind(uint32_t slot = 0) const override;

            void BindImage(uint32_t unit, TextureAccess access, uint32_t world = 0) const override;

            void ReadPixels(void* outData, size_t bufferSize, uint32_t world = 0) const override;

            bool IsValid() const override;

            bool UpdateRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* pixels, uint32_t rowPitchBytes = 0) override;

            bool SwapContents(Texture2D& other) override;

            uint32_t GetHandle() const override { return ID; }

            uint64_t GetBindlessHandle() const override { return GetBindlessHandle(ID); }

            static uint64_t GetBindlessHandle(uint32_t glTextureID);

            // A bindless handle freezes the texture (TexImage* gives INVALID_OPERATION), so re-specifying means recreating it.
            static bool HasBindlessHandle(uint32_t glTextureID);

            // A handle can never be destroyed, so delete the texture object right after this.
            static void ReleaseBindlessHandle(uint32_t glTextureID);

            ~GLTexture2D();

        private:

            uint32_t ID;

            uint32_t m_GLInternalFormat = 0;

            int64_t m_GpuBytes = 0;

    };

}
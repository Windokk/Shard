#pragma once

#include "engine/renderer/rhi/resources/texture/texture.hpp"

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

            void Bind(uint32_t slot = 0) const override;

            void BindImage(uint32_t unit, TextureAccess access, uint32_t level = 0) const override;

            void ReadPixels(void* outData, size_t bufferSize, uint32_t level = 0) const override;

            bool IsValid() const override;

            uint32_t GetHandle() const override { return ID; }

            // Returns a resident ARB_bindless_texture handle for the given raw GL texture ID, creating
            // and residency-registering it on first use (cached thereafter). Returns 0 if glTextureID is
            // 0 or GL_ARB_bindless_texture isn't supported by the driver - callers should treat 0 as
            // "no texture available" and fall back to flat scalar values instead.
            static uint64_t GetBindlessHandle(uint32_t glTextureID);

            // Whether a bindless handle has already been minted for this raw GL texture ID.
            // ARB_bindless_texture freezes a texture as soon as a handle references it - TexImage* on it
            // is an INVALID_OPERATION from then on - so anything that needs to re-specify a texture
            // (Framebuffer::Resize) has to check this and recreate the texture object instead.
            static bool HasBindlessHandle(uint32_t glTextureID);

            // Makes this texture's handle non-resident and forgets it. A handle can never actually be
            // destroyed, so the caller must delete the texture object right after - the replacement ID
            // then gets a fresh handle from the next GetBindlessHandle() call.
            static void ReleaseBindlessHandle(uint32_t glTextureID);

            ~GLTexture2D();

        private:

            uint32_t ID;

            uint32_t m_GLInternalFormat = 0;

    };

}
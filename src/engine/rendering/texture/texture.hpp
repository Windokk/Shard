#pragma once

#include <vector>

#include "engine/rendering/utils.hpp"

#include "engine/filesystem/filesystem.hpp"

namespace Shard::Engine::Rendering {
    
    enum class TextureFilter
    {
        Linear,
        LinearMipmapLinear,
        LinearMipmapNearest,
        Nearest,
        NearestMipmapLinear,
        NearestMipmapNearest
    };

    enum class TextureWrap
    {
        Repeat,
        ClampEdge,
        ClampBorder,
        Mirror
    };

    enum class TextureInternalFormat
    {
        RED,
        R16F,
        RG,
        RG16F,
        RGB,
        RGB8,
        RGB16F,
        RGB32I,
        RGB32F,
        RGBA8,
        RGBA,
        RGBA16F,
        RGBA32I,
        RGBA32F,
        Depth24Stencil8,
        Depth16,
        Depth24,
        Depth32,
        Depth32F
    };

    enum class TextureFormat
    {
        RED,
        RG,
        RGB,
        RGBA,
        Depth
    };

    enum class TextureCompareFunc
    {
        Less,
        LessOrEqual,
        Greater,
        Always,
        Never
    };

    enum class TextureCompareMode
    {
        None,
        CompareRefToTexture
    };

    // Access qualifier for binding a texture as an image unit (glBindImageTexture) rather than as a
    // sampler - this is how a compute shader reads/writes a texture directly (e.g. a raytracer's
    // output image), as opposed to the filtered/interpolated sampling done through Bind().
    enum class TextureAccess
    {
        ReadOnly,
        WriteOnly,
        ReadWrite
    };

    struct TextureSpecifications
    {
        uint32_t width = 0;
        uint32_t height = 0;

        TextureFormat format = TextureFormat::RGBA;
        TextureInternalFormat internalFormat = TextureInternalFormat::RGBA;

        TextureFilter minFilter = TextureFilter::Linear;
        TextureFilter magFilter = TextureFilter::Linear;

        TextureWrap wrapS = TextureWrap::Repeat;
        TextureWrap wrapT = TextureWrap::Repeat;
        TextureWrap wrapR = TextureWrap::Repeat;

        COL_RGBA borderColor = COL_RGBA(-1.0f);

        TextureCompareFunc compareFunc = TextureCompareFunc::Less;
        TextureCompareMode compareMode = TextureCompareMode::None;

        bool generateMips = true;

        // Allocates storage with glTexStorage2D (immutable) instead of glTexImage2D. Required in
        // practice for a texture a compute shader will bind as an image (BindImage) - it pins down a
        // fixed format/mip count up front, which is what image load/store needs to stay well-defined.
        bool immutableStorage = false;

        // The pixel data handed to Texture2D::Create() and returned by ReadPixels() is packed 16-bit
        // half floats (GL_HALF_FLOAT) instead of the 32-bit floats every *16F format uses by default.
        // Only meaningful for R16F/RG16F/RGB16F/RGBA16F. Lets a half-float texture be saved to and
        // restored from disk byte-for-byte (see ProbeBakeData) with no float<->half conversion, and makes
        // GetPixelDataSize() the true readback size - with the default float path it under-reports a
        // *16F texture's ReadPixels() output by half.
        bool halfFloatPixelData = false;
    };

    // Result of decoding an image file into CPU memory, with no GL calls involved - safe to produce
    // on a worker thread. `pixels` owns its own copy of the decoded data (not a raw stbi pointer), so
    // it can be handed across a thread boundary and consumed later by Texture2D::Create(spec, data).
    struct TextureDecodeResult
    {
        bool success = false;
        int width = 0;
        int height = 0;
        TextureInternalFormat format = TextureInternalFormat::RGBA;
        std::vector<unsigned char> pixels;
    };

    // Pure CPU decode (file read + stb_image decode) of an image file - no GL calls, safe to call
    // from any thread. Used both by Texture2D::Create(spec, filepath) and by the async level loader's
    // background decode workers.
    TextureDecodeResult DecodeTextureFile(const Filesystem::Path& filepath);

    class Texture2D
    {
        public:

            virtual ~Texture2D() = default;

            virtual void Bind(uint32_t slot = 0) const = 0;

            // Binds this texture to an image unit for use with image load/store in a shader (e.g.
            // `imageStore`/`imageLoad` in a compute shader), as opposed to sampled access via Bind().
            // Requires the texture to have been created with immutableStorage = true.
            virtual void BindImage(uint32_t unit, TextureAccess access, uint32_t level = 0) const = 0;

            // Blocking readback of this texture's pixel data into CPU memory (e.g. to export a
            // compute-shader-rendered image to disk). outData must be at least GetPixelDataSize(level)
            // bytes. If a compute shader wrote to this texture via BindImage, a
            // MemoryBarrierBit::TextureUpdate barrier must be issued after the dispatch and before this
            // call, or the read may return stale data.
            virtual void ReadPixels(void* outData, size_t bufferSize, uint32_t level = 0) const = 0;

            // Size in bytes of a full ReadPixels() call for the given mip level, based on this
            // texture's format - use this to size the buffer passed to ReadPixels().
            size_t GetPixelDataSize(uint32_t level = 0) const;

            uint32_t GetWidth() const { return m_Specifications.width; }
            uint32_t GetHeight() const { return m_Specifications.height; }

            virtual uint32_t GetHandle() const = 0;

            virtual bool IsValid() const = 0;

            const TextureSpecifications& GetSpecifications() const { return m_Specifications; };

            static std::shared_ptr<Texture2D> Create(
                TextureSpecifications& spec,
                const void* data
            );

            static std::shared_ptr<Texture2D> Create(
                TextureSpecifications& spec,
                const Filesystem::Path& filepath
            );

            void SetAssetID(Filesystem::AssetID assetID) {
                this->assetID = assetID;
            }

            Filesystem::AssetID GetAssetID() {
                return this->assetID;
            }
        protected:
            Filesystem::AssetID assetID;

            TextureSpecifications m_Specifications;
    };
}
#pragma once

#include <memory>
#include <vector>

#include "engine/core/color.hpp"

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/renderer/rhi/resources/texture/texture_compression.hpp"

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
        Depth32F,
        R32F
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

        bool immutableStorage = false;

        bool halfFloatPixelData = false;
    };

    struct TextureDecodeResult
    {
        bool success = false;
        int width = 0;
        int height = 0;
        TextureInternalFormat format = TextureInternalFormat::RGBA;
        std::vector<unsigned char> pixels;

        std::shared_ptr<CompressedImage> compressed;
    };

    TextureDecodeResult DecodeTextureFile(const Filesystem::Path& filepath);

    TextureDecodeResult DecodeTextureFileForUpload(const Filesystem::Path& filepath, unsigned threads = 1);

    uint32_t MipsToSkip(uint32_t width, uint32_t height);

    class Texture2D
    {
        public:

            virtual ~Texture2D() = default;

            virtual void Bind(uint32_t slot = 0) const = 0;

            virtual void BindImage(uint32_t unit, TextureAccess access, uint32_t world = 0) const = 0;

            virtual void ReadPixels(void* outData, size_t bufferSize, uint32_t world = 0) const = 0;

            size_t GetPixelDataSize(uint32_t world = 0) const;

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

            static std::shared_ptr<Texture2D> Create(
                const TextureSpecifications& spec,
                const CompressedImage& image,
                uint32_t firstMip = 0
            );

            static std::shared_ptr<Texture2D> Create(
                TextureSpecifications& spec,
                const TextureDecodeResult& decoded
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
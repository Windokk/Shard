#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Shard::Engine::Rendering {

    enum class BlockFormat : uint8_t {
        BC4 = 1,
        BC5 = 2,
        BC7 = 3,
    };

    inline size_t BlockBytes(BlockFormat format) { return format == BlockFormat::BC4 ? 8 : 16; }
    inline uint32_t BlocksAcross(uint32_t pixels) { return (pixels + 3) / 4; }

    struct CompressedImage {
        uint32_t width = 0;
        uint32_t height = 0;
        BlockFormat format = BlockFormat::BC7;
        std::vector<std::vector<uint8_t>> mips;

        size_t ByteSize() const
        {
            size_t total = 0;
            for (const auto& mip : mips)
                total += mip.size();
            return total;
        }
    };

    uint32_t MipCount(uint32_t width, uint32_t height);

    inline size_t MipBytes(BlockFormat format, uint32_t width, uint32_t height)
    {
        return size_t(BlocksAcross(width)) * BlocksAcross(height) * BlockBytes(format);
    }

    bool ShouldCompress(uint32_t width, uint32_t height);

    bool LooksLikeNormalMap(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t channels);

    bool CompressImage(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t channels, CompressedImage& out, bool normalMap = false, unsigned threads = 1, bool srgb = false);

    void EncodeBC7Block(const uint8_t* rgba, uint8_t* out);

    bool DecodeBC7Block(const uint8_t* block, uint8_t* rgba);

    namespace TextureCache {

        void SetDirectory(const std::string& directory);
        const std::string& GetDirectory();

        std::string MakeKey(const std::string& sourcePath, bool srgb = false, uint64_t variant = 0);

        // A texture cooked by ShardCooker sits next to its source : <source>.lin.stx (data) or <source>.srgb.stx (colour)
        std::string CookedPath(const std::string& sourcePath, bool srgb);

        bool Load(const std::string& key, CompressedImage& out);
        void Store(const std::string& key, const CompressedImage& image);

        constexpr uint32_t kVersion = 2;

        std::vector<uint8_t> Serialize(const CompressedImage& image);
        bool Deserialize(const uint8_t* data, size_t size, CompressedImage& out);
    }
}

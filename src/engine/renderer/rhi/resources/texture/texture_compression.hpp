#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Shard::Engine::Rendering {

    /// Block compressed formats the engine writes. They are the desktop GPU formats : BC4 (RGTC1) one channel, BC5 (RGTC2)
    /// two channels, BC7 (BPTC) four channels. A BC7 texel costs 1 byte and a BC4 texel half of that, against 4 / 1
    /// bytes for the same image uncompressed (and GL pads RGB8 to 4).
    enum class BlockFormat : uint8_t {
        BC4 = 1,
        BC5 = 2,
        BC7 = 3,
    };

    inline size_t BlockBytes(BlockFormat format) { return format == BlockFormat::BC4 ? 8 : 16; }
    inline uint32_t BlocksAcross(uint32_t pixels) { return (pixels + 3) / 4; }

    /// An image already encoded in blocks, with all its mip levels : what is uploaded to the GPU as is.
    struct CompressedImage {
        uint32_t width = 0;
        uint32_t height = 0;
        BlockFormat format = BlockFormat::BC7;
        std::vector<std::vector<uint8_t>> mips;         // mips[0] is width x height, each next one half the size down to 1x1

        size_t ByteSize() const
        {
            size_t total = 0;
            for (const auto& mip : mips)
                total += mip.size();
            return total;
        }
    };

    /// Number of levels of a full mip chain of a width x height image
    uint32_t MipCount(uint32_t width, uint32_t height);

    /// Bytes of one level of a block compressed image
    inline size_t MipBytes(BlockFormat format, uint32_t width, uint32_t height)
    {
        return size_t(BlocksAcross(width)) * BlocksAcross(height) * BlockBytes(format);
    }

    /// Whether an image of that size is worth the trouble : small images (icons, noise, LUTs) stay as they are
    bool ShouldCompress(uint32_t width, uint32_t height);

    /// Whether an 8 bit image (3 or 4 channels, tightly packed) is a tangent-space normal map : nearly every pixel, read as
    /// a vector, is a unit vector pointing out of the surface. Judged on a sample of the pixels.
    bool LooksLikeNormalMap(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t channels);

    /// @brief Builds the mip chain of an 8 bit image (`channels` 1 to 4, tightly packed, any row order) and encodes
    /// every level : 1 channel -> BC4, 2 -> BC5, 3 and 4 -> BC7.
    /// @param normalMap  a tangent-space normal map : only x and y are kept (BC5, twice the precision per channel of what a
    /// single colour line can give a normal), the shaders rebuild z. Needs 3 or 4 channels.
    /// @param threads  how many threads share the work (1 : the caller's thread only)
    bool CompressImage(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t channels, CompressedImage& out, bool normalMap = false, unsigned threads = 1);

    /// BC7, mode 6 only : one line through colour space, 16 positions on it, alpha included. `rgba` is 16 pixels
    /// (row major, 4 bytes each), `out` the 16 bytes of the block.
    void EncodeBC7Block(const uint8_t* rgba, uint8_t* out);

    /// Inverse of EncodeBC7Block, for tests and tools. False if the block is not mode 6.
    bool DecodeBC7Block(const uint8_t* block, uint8_t* rgba);

    /// What is kept on disk so that an image is only ever encoded once
    namespace TextureCache {

        /// Where the cached files live (empty : no cache). Set once the project is known.
        void SetDirectory(const std::string& directory);
        const std::string& GetDirectory();

        /// The cache key of a source image, from its path, its size and its modification time. Empty if the file cannot
        /// be inspected or there is no cache.
        std::string MakeKey(const std::string& sourcePath);

        bool Load(const std::string& key, CompressedImage& out);
        void Store(const std::string& key, const CompressedImage& image);

        /// Bump when the encoder or the format changes : older files are then ignored
        constexpr uint32_t kVersion = 2;

        /// Serialization of one cache file (also what the tests round trip)
        std::vector<uint8_t> Serialize(const CompressedImage& image);
        bool Deserialize(const uint8_t* data, size_t size, CompressedImage& out);
    }
}

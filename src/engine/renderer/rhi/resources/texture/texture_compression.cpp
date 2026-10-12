#include "texture_compression.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#define STB_DXT_IMPLEMENTATION
#define STB_DXT_STATIC
#include <stb/stb_dxt.h>

namespace Shard::Engine::Rendering {

    namespace {

        // BPTC's 4 bit index weights, out of 64
        constexpr int kWeights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

        constexpr uint32_t kMinCompressSize = 128;

        // Writes `count` bits of `value`, lowest bit first, into a 128 bit block
        struct BitWriter {
            uint64_t lo = 0, hi = 0;
            int position = 0;

            void Put(uint32_t value, int count)
            {
                for (int i = 0; i < count; i++, position++)
                {
                    const uint64_t bit = (value >> i) & 1u;
                    if (position < 64)
                        lo |= bit << position;
                    else
                        hi |= bit << (position - 64);
                }
            }
        };

        struct BitReader {
            uint64_t lo, hi;
            int position = 0;

            uint32_t Get(int count)
            {
                uint32_t value = 0;
                for (int i = 0; i < count; i++, position++)
                {
                    const uint64_t bit = position < 64 ? (lo >> position) & 1u : (hi >> (position - 64)) & 1u;
                    value |= uint32_t(bit) << i;
                }
                return value;
            }
        };

        struct Endpoint {
            int q[4] = {0, 0, 0, 0};    // 7 bit values
            int p = 0;                  // shared low bit
            int Value(int c) const { return q[c] * 2 + p; }
        };

        // The 16 colours of the palette between two endpoints, as the decoder computes them
        void BuildPalette(const Endpoint& e0, const Endpoint& e1, int palette[16][4])
        {
            for (int k = 0; k < 16; k++)
                for (int c = 0; c < 4; c++)
                    palette[k][c] = ((64 - kWeights4[k]) * e0.Value(c) + kWeights4[k] * e1.Value(c) + 32) >> 6;
        }

        // Quantizes a float endpoint to 7 bits + a p-bit. Alpha, when it is the same over the whole block, is stored
        // exactly, which also fixes the p-bit (it is the low bit of every channel, alpha included).
        Endpoint Quantize(const float v[4], bool exactAlpha, int alpha)
        {
            Endpoint best;
            float bestError = 1e30f;
            for (int p = 0; p < 2; p++)
            {
                if (exactAlpha && p != (alpha & 1))
                    continue;

                Endpoint e;
                e.p = p;
                float error = 0.0f;
                for (int c = 0; c < 4; c++)
                {
                    if (c == 3 && exactAlpha)
                    {
                        e.q[3] = alpha >> 1;
                        continue;
                    }
                    const int q = std::clamp(int(std::lround((v[c] - float(p)) * 0.5f)), 0, 127);
                    e.q[c] = q;
                    const float diff = float(q * 2 + p) - v[c];
                    error += diff * diff;
                }
                if (error < bestError)
                {
                    bestError = error;
                    best = e;
                }
            }
            return best;
        }

        int DistanceSquared(const int a[4], const uint8_t* b, int dims)
        {
            int total = 0;
            for (int c = 0; c < dims; c++)
            {
                const int d = a[c] - int(b[c]);
                total += d * d;
            }
            return total;
        }

        // Nearest palette entry of every pixel, and the total squared error
        int AssignIndices(const uint8_t* rgba, const int palette[16][4], int dims, int indices[16])
        {
            const int* first = palette[0];
            int axis[4];
            int axisLength2 = 0;
            for (int c = 0; c < 4; c++)
            {
                axis[c] = palette[15][c] - first[c];
                axisLength2 += axis[c] * axis[c];
            }

            int total = 0;
            for (int i = 0; i < 16; i++)
            {
                const uint8_t* px = rgba + i * 4;
                int guess = 0;
                if (axisLength2 > 0)
                {
                    int dot = 0;
                    for (int c = 0; c < dims; c++)
                        dot += (int(px[c]) - first[c]) * axis[c];
                    guess = std::clamp(int(std::lround(float(dot) / float(axisLength2) * 15.0f)), 0, 15);
                }

                int bestIndex = guess;
                int bestError = DistanceSquared(palette[guess], px, dims);
                for (int k = std::max(0, guess - 1); k <= std::min(15, guess + 1); k++)
                {
                    const int error = DistanceSquared(palette[k], px, dims);
                    if (error < bestError)
                    {
                        bestError = error;
                        bestIndex = k;
                    }
                }
                indices[i] = bestIndex;
                total += bestError;
            }
            return total;
        }

        // Least squares endpoints for fixed indices. False when the indices do not pin them down (all the same weight).
        bool RefitEndpoints(const uint8_t* rgba, const int indices[16], int dims, float e0[4], float e1[4])
        {
            float aa = 0, ab = 0, bb = 0;
            float ax[4] = {0, 0, 0, 0}, bx[4] = {0, 0, 0, 0};
            for (int i = 0; i < 16; i++)
            {
                const float b = float(kWeights4[indices[i]]) / 64.0f;
                const float a = 1.0f - b;
                aa += a * a;
                ab += a * b;
                bb += b * b;
                for (int c = 0; c < dims; c++)
                {
                    const float v = float(rgba[i * 4 + c]);
                    ax[c] += a * v;
                    bx[c] += b * v;
                }
            }
            const float det = aa * bb - ab * ab;
            if (std::fabs(det) < 1e-4f)
                return false;
            for (int c = 0; c < dims; c++)
            {
                e0[c] = (ax[c] * bb - bx[c] * ab) / det;
                e1[c] = (bx[c] * aa - ax[c] * ab) / det;
            }
            return true;
        }

        // sRGB <-> linear for the 8 bit colours of a colour texture : the decode is a 256 entry table, the encode a 4096 entry
        // table indexed by the linear value (twice the precision of the finest step the curve has, near black)
        struct SRGBTables
        {
            float toLinear[256];
            uint8_t toSRGB[4096];

            SRGBTables()
            {
                for (int i = 0; i < 256; i++)
                {
                    const float c = float(i) / 255.0f;
                    toLinear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
                }
                for (int i = 0; i < 4096; i++)
                {
                    const float l = float(i) / 4095.0f;
                    const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
                    toSRGB[i] = uint8_t(std::clamp(int(c * 255.0f + 0.5f), 0, 255));
                }
            }
        };

        const SRGBTables& GetSRGBTables()
        {
            static const SRGBTables tables;
            return tables;
        }

        // The block downsampled by two, with the border pixels repeated for an odd size. `srgb` : the colour channels (the first
        // three of a 3 or 4 channel image) are averaged as light, not as encoded values.
        void Downsample(const uint8_t* src, uint32_t width, uint32_t height, uint32_t channels, std::vector<uint8_t>& dst, uint32_t& newWidth, uint32_t& newHeight, bool srgb = false)
        {
            const uint32_t colourChannels = (srgb && channels >= 3) ? 3 : 0;
            const SRGBTables& tables = GetSRGBTables();

            newWidth = std::max(1u, width / 2);
            newHeight = std::max(1u, height / 2);
            dst.resize(size_t(newWidth) * newHeight * channels);
            for (uint32_t y = 0; y < newHeight; y++)
            {
                const uint32_t y0 = std::min(2 * y, height - 1);
                const uint32_t y1 = std::min(2 * y + 1, height - 1);
                for (uint32_t x = 0; x < newWidth; x++)
                {
                    const uint32_t x0 = std::min(2 * x, width - 1);
                    const uint32_t x1 = std::min(2 * x + 1, width - 1);
                    const uint8_t* p00 = src + (size_t(y0) * width + x0) * channels;
                    const uint8_t* p10 = src + (size_t(y0) * width + x1) * channels;
                    const uint8_t* p01 = src + (size_t(y1) * width + x0) * channels;
                    const uint8_t* p11 = src + (size_t(y1) * width + x1) * channels;
                    uint8_t* out = &dst[(size_t(y) * newWidth + x) * channels];
                    for (uint32_t c = 0; c < channels; c++)
                    {
                        if (c < colourChannels)
                        {
                            const float light = 0.25f * (tables.toLinear[p00[c]] + tables.toLinear[p10[c]] + tables.toLinear[p01[c]] + tables.toLinear[p11[c]]);
                            out[c] = tables.toSRGB[std::clamp(int(light * 4095.0f + 0.5f), 0, 4095)];
                        }
                        else
                            out[c] = uint8_t((int(p00[c]) + int(p10[c]) + int(p01[c]) + int(p11[c]) + 2) >> 2);
                    }
                }
            }
        }

        // Encodes one mip level into blocks, rows [rowBegin, rowEnd) of blocks
        void EncodeRows(const uint8_t* src, uint32_t width, uint32_t height, uint32_t channels, BlockFormat format, uint8_t* dst, uint32_t rowBegin, uint32_t rowEnd)
        {
            const uint32_t blocksX = BlocksAcross(width);
            const size_t blockBytes = BlockBytes(format);

            for (uint32_t by = rowBegin; by < rowEnd; by++)
            {
                for (uint32_t bx = 0; bx < blocksX; bx++)
                {
                    uint8_t gathered[64];

                    for (uint32_t py = 0; py < 4; py++)
                    {
                        const uint32_t y = std::min(by * 4 + py, height - 1);
                        for (uint32_t px = 0; px < 4; px++)
                        {
                            const uint32_t x = std::min(bx * 4 + px, width - 1);
                            const uint8_t* s = src + (size_t(y) * width + x) * channels;
                            uint8_t* g = gathered;
                            const uint32_t i = py * 4 + px;
                            switch (format)
                            {
                                case BlockFormat::BC4:
                                    g[i] = s[0];
                                    break;
                                case BlockFormat::BC5:
                                    g[i * 2] = s[0];
                                    g[i * 2 + 1] = s[1];
                                    break;
                                case BlockFormat::BC7:
                                    g[i * 4] = s[0];
                                    g[i * 4 + 1] = channels > 1 ? s[1] : s[0];
                                    g[i * 4 + 2] = channels > 2 ? s[2] : s[0];
                                    g[i * 4 + 3] = channels > 3 ? s[3] : 255;
                                    break;
                            }
                        }
                    }

                    uint8_t* out = dst + (size_t(by) * blocksX + bx) * blockBytes;
                    switch (format)
                    {
                        case BlockFormat::BC4: stb_compress_bc4_block(out, gathered); break;
                        case BlockFormat::BC5: stb_compress_bc5_block(out, gathered); break;
                        case BlockFormat::BC7: EncodeBC7Block(gathered, out); break;
                    }
                }
            }
        }

        uint64_t Fnv1a64(const std::string& text)
        {
            uint64_t hash = 1469598103934665603ULL;
            for (unsigned char c : text)
            {
                hash ^= c;
                hash *= 1099511628211ULL;
            }
            return hash;
        }

        std::mutex g_CacheMutex;
        std::string g_CacheDirectory;
    }

    uint32_t MipCount(uint32_t width, uint32_t height)
    {
        uint32_t levels = 1;
        uint32_t size = std::max(width, height);
        while (size > 1)
        {
            size >>= 1;
            levels++;
        }
        return levels;
    }

    bool ShouldCompress(uint32_t width, uint32_t height)
    {
        return width >= kMinCompressSize && height >= kMinCompressSize;
    }

    void EncodeBC7Block(const uint8_t* rgba, uint8_t* out)
    {
        // A block whose alpha never changes does not spend any of its precision on it
        int alphaMin = 255, alphaMax = 0;
        for (int i = 0; i < 16; i++)
        {
            alphaMin = std::min(alphaMin, int(rgba[i * 4 + 3]));
            alphaMax = std::max(alphaMax, int(rgba[i * 4 + 3]));
        }
        const bool exactAlpha = alphaMin == alphaMax;
        const int dims = exactAlpha ? 3 : 4;

        // Principal axis of the 16 colours (power iteration on their covariance)
        float mean[4] = {0, 0, 0, 0};
        for (int i = 0; i < 16; i++)
            for (int c = 0; c < dims; c++)
                mean[c] += float(rgba[i * 4 + c]);
        for (int c = 0; c < dims; c++)
            mean[c] /= 16.0f;

        float covariance[4][4] = {};
        for (int i = 0; i < 16; i++)
        {
            float d[4];
            for (int c = 0; c < dims; c++)
                d[c] = float(rgba[i * 4 + c]) - mean[c];
            for (int a = 0; a < dims; a++)
                for (int b = 0; b < dims; b++)
                    covariance[a][b] += d[a] * d[b];
        }

        float axis[4] = {0, 0, 0, 0};
        {
            int lo[4] = {255, 255, 255, 255}, hi[4] = {0, 0, 0, 0};
            for (int i = 0; i < 16; i++)
                for (int c = 0; c < dims; c++)
                {
                    lo[c] = std::min(lo[c], int(rgba[i * 4 + c]));
                    hi[c] = std::max(hi[c], int(rgba[i * 4 + c]));
                }
            for (int c = 0; c < dims; c++)
                axis[c] = float(hi[c] - lo[c]) + 0.01f;
        }
        for (int iteration = 0; iteration < 6; iteration++)
        {
            float next[4] = {0, 0, 0, 0};
            for (int a = 0; a < dims; a++)
                for (int b = 0; b < dims; b++)
                    next[a] += covariance[a][b] * axis[b];
            float length = 0.0f;
            for (int c = 0; c < dims; c++)
                length += next[c] * next[c];
            length = std::sqrt(length);
            if (length < 1e-6f)
                break;
            for (int c = 0; c < dims; c++)
                axis[c] = next[c] / length;
        }
        {
            float length = 0.0f;
            for (int c = 0; c < dims; c++)
                length += axis[c] * axis[c];
            length = std::sqrt(length);
            for (int c = 0; c < dims; c++)
                axis[c] /= length;
        }

        float tMin = 1e30f, tMax = -1e30f;
        for (int i = 0; i < 16; i++)
        {
            float t = 0.0f;
            for (int c = 0; c < dims; c++)
                t += (float(rgba[i * 4 + c]) - mean[c]) * axis[c];
            tMin = std::min(tMin, t);
            tMax = std::max(tMax, t);
        }

        float e0[4], e1[4];
        for (int c = 0; c < 4; c++)
        {
            e0[c] = c < dims ? std::clamp(mean[c] + axis[c] * tMin, 0.0f, 255.0f) : float(alphaMin);
            e1[c] = c < dims ? std::clamp(mean[c] + axis[c] * tMax, 0.0f, 255.0f) : float(alphaMin);
        }

        // Quantize, assign, refit : keep the best of a few rounds
        Endpoint bestE0, bestE1;
        int bestIndices[16] = {};
        int bestError = 1 << 30;

        for (int round = 0; round < 3; round++)
        {
            const Endpoint q0 = Quantize(e0, exactAlpha, alphaMin);
            const Endpoint q1 = Quantize(e1, exactAlpha, alphaMin);

            int palette[16][4];
            BuildPalette(q0, q1, palette);
            int indices[16];
            const int error = AssignIndices(rgba, palette, dims, indices);

            if (error < bestError)
            {
                bestError = error;
                bestE0 = q0;
                bestE1 = q1;
                std::memcpy(bestIndices, indices, sizeof(indices));
            }
            if (error == 0)
                break;

            float r0[4] = {e0[0], e0[1], e0[2], e0[3]}, r1[4] = {e1[0], e1[1], e1[2], e1[3]};
            if (!RefitEndpoints(rgba, indices, dims, r0, r1))
                break;
            for (int c = 0; c < dims; c++)
            {
                e0[c] = std::clamp(r0[c], 0.0f, 255.0f);
                e1[c] = std::clamp(r1[c], 0.0f, 255.0f);
            }
        }

        // The first pixel's index only has 3 bits : its top bit is implied to be 0, so flip the line if it is not
        if (bestIndices[0] >= 8)
        {
            std::swap(bestE0, bestE1);
            for (int i = 0; i < 16; i++)
                bestIndices[i] = 15 - bestIndices[i];
        }

        BitWriter writer;
        writer.Put(1u << 6, 7);                                     // mode 6 : six zeros then a one
        for (int c = 0; c < 4; c++)
        {
            writer.Put(uint32_t(bestE0.q[c]), 7);
            writer.Put(uint32_t(bestE1.q[c]), 7);
        }
        writer.Put(uint32_t(bestE0.p), 1);
        writer.Put(uint32_t(bestE1.p), 1);
        for (int i = 0; i < 16; i++)
            writer.Put(uint32_t(bestIndices[i]), i == 0 ? 3 : 4);

        std::memcpy(out, &writer.lo, 8);
        std::memcpy(out + 8, &writer.hi, 8);
    }

    bool DecodeBC7Block(const uint8_t* block, uint8_t* rgba)
    {
        BitReader reader;
        std::memcpy(&reader.lo, block, 8);
        std::memcpy(&reader.hi, block + 8, 8);

        if (reader.Get(7) != (1u << 6))
            return false;

        Endpoint e0, e1;
        for (int c = 0; c < 4; c++)
        {
            e0.q[c] = int(reader.Get(7));
            e1.q[c] = int(reader.Get(7));
        }
        e0.p = int(reader.Get(1));
        e1.p = int(reader.Get(1));

        int palette[16][4];
        BuildPalette(e0, e1, palette);

        for (int i = 0; i < 16; i++)
        {
            const int index = int(reader.Get(i == 0 ? 3 : 4));
            for (int c = 0; c < 4; c++)
                rgba[i * 4 + c] = uint8_t(palette[index][c]);
        }
        return true;
    }

    bool LooksLikeNormalMap(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t channels)
    {
        if (!pixels || channels < 3 || width == 0 || height == 0)
            return false;

        // A grid of at most ~4096 pixels over the whole image
        const uint32_t stepX = std::max(1u, width / 64);
        const uint32_t stepY = std::max(1u, height / 64);
        // What sets a normal map apart is how exact its vectors are : the length of a pixel read as a vector is 1 within
        // the 8 bit rounding (a photograph, an albedo, a mask are nowhere near), and on average it points out of the surface
        uint32_t samples = 0, unitLength = 0;
        double sumZ = 0.0;
        for (uint32_t y = 0; y < height; y += stepY)
        {
            for (uint32_t x = 0; x < width; x += stepX)
            {
                const uint8_t* p = pixels + (size_t(y) * width + x) * channels;
                const float nx = float(p[0]) / 127.5f - 1.0f;
                const float ny = float(p[1]) / 127.5f - 1.0f;
                const float nz = float(p[2]) / 127.5f - 1.0f;
                const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
                samples++;
                sumZ += nz;
                if (std::fabs(length - 1.0f) < 0.05f)
                    unitLength++;
            }
        }
        return samples > 0 && unitLength * 100 >= samples * 95 && sumZ / samples > 0.3;
    }

    bool CompressImage(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t channels, CompressedImage& out, bool normalMap, unsigned threads, bool srgb)
    {
        if (!pixels || width == 0 || height == 0 || channels < 1 || channels > 4)
            return false;
        if (normalMap && channels < 3)
            return false;

        out = CompressedImage();
        out.width = width;
        out.height = height;
        out.format = normalMap ? BlockFormat::BC5
                   : channels == 1 ? BlockFormat::BC4 : channels == 2 ? BlockFormat::BC5 : BlockFormat::BC7;

        const uint32_t levels = MipCount(width, height);
        out.mips.reserve(levels);

        // Level 0 is read where the caller has it : only the smaller levels are built (a 4096 x 4096 RGB image is 48 MB)
        const uint8_t* current = pixels;
        std::vector<uint8_t> storage, next;
        uint32_t levelWidth = width, levelHeight = height;

        for (uint32_t level = 0; level < levels; level++)
        {
            std::vector<uint8_t> blocks(MipBytes(out.format, levelWidth, levelHeight));
            const uint32_t rows = BlocksAcross(levelHeight);

            const unsigned workers = (threads > 1 && rows >= 64) ? std::min<unsigned>(threads, rows / 16) : 1;
            if (workers <= 1)
            {
                EncodeRows(current, levelWidth, levelHeight, channels, out.format, blocks.data(), 0, rows);
            }
            else
            {
                std::vector<std::thread> pool;
                pool.reserve(workers);
                for (unsigned w = 0; w < workers; w++)
                {
                    const uint32_t begin = rows * w / workers;
                    const uint32_t end = rows * (w + 1) / workers;
                    pool.emplace_back([&, begin, end] {
                        EncodeRows(current, levelWidth, levelHeight, channels, out.format, blocks.data(), begin, end);
                    });
                }
                for (std::thread& t : pool)
                    t.join();
            }
            out.mips.push_back(std::move(blocks));

            if (level + 1 < levels)
            {
                uint32_t nextWidth, nextHeight;
                Downsample(current, levelWidth, levelHeight, channels, next, nextWidth, nextHeight, srgb && !normalMap);
                storage.swap(next);
                current = storage.data();
                levelWidth = nextWidth;
                levelHeight = nextHeight;
            }
        }
        return true;
    }

    namespace TextureCache {

        void SetDirectory(const std::string& directory)
        {
            std::lock_guard<std::mutex> lock(g_CacheMutex);
            g_CacheDirectory = directory;
        }

        const std::string& GetDirectory()
        {
            return g_CacheDirectory;
        }

        std::string MakeKey(const std::string& sourcePath, bool srgb)
        {
            std::string directory;
            {
                std::lock_guard<std::mutex> lock(g_CacheMutex);
                directory = g_CacheDirectory;
            }
            if (directory.empty())
                return "";

            std::error_code ec;
            const auto size = std::filesystem::file_size(sourcePath, ec);
            if (ec)
                return "";
            const auto stamp = std::filesystem::last_write_time(sourcePath, ec);
            if (ec)
                return "";

            // The key of a linear texture is what it always was : the files already cached stay valid
            const std::string identity = std::to_string(kVersion) + "|" + sourcePath + "|" + std::to_string(size) + "|" + std::to_string(stamp.time_since_epoch().count()) + (srgb ? "|srgb" : "");
            char name[32];
            std::snprintf(name, sizeof(name), "%016llx.stx", static_cast<unsigned long long>(Fnv1a64(identity)));
            return (std::filesystem::path(directory) / name).generic_string();
        }

        std::vector<uint8_t> Serialize(const CompressedImage& image)
        {
            std::vector<uint8_t> bytes;
            bytes.reserve(32 + image.ByteSize() + image.mips.size() * 8);
            auto put = [&bytes](const void* data, size_t size) {
                const uint8_t* p = static_cast<const uint8_t*>(data);
                bytes.insert(bytes.end(), p, p + size);
            };
            const char magic[4] = {'S', 'T', 'X', 'C'};
            const uint32_t version = kVersion;
            const uint8_t format = uint8_t(image.format);
            const uint8_t padding[3] = {0, 0, 0};
            const uint32_t mipCount = uint32_t(image.mips.size());
            put(magic, 4);
            put(&version, 4);
            put(&image.width, 4);
            put(&image.height, 4);
            put(&format, 1);
            put(padding, 3);
            put(&mipCount, 4);
            for (const auto& mip : image.mips)
            {
                const uint64_t size = mip.size();
                put(&size, 8);
                put(mip.data(), mip.size());
            }
            return bytes;
        }

        bool Deserialize(const uint8_t* data, size_t size, CompressedImage& out)
        {
            size_t position = 0;
            auto take = [&](void* dst, size_t count) {
                if (position + count > size)
                    return false;
                std::memcpy(dst, data + position, count);
                position += count;
                return true;
            };

            char magic[4];
            uint32_t version = 0, width = 0, height = 0, mipCount = 0;
            uint8_t format = 0, padding[3];
            if (!take(magic, 4) || std::memcmp(magic, "STXC", 4) != 0 || !take(&version, 4) || version != kVersion ||
                !take(&width, 4) || !take(&height, 4) || !take(&format, 1) || !take(padding, 3) || !take(&mipCount, 4))
                return false;
            if (width == 0 || height == 0 || format < 1 || format > 3 || mipCount != MipCount(width, height))
                return false;

            CompressedImage image;
            image.width = width;
            image.height = height;
            image.format = BlockFormat(format);
            image.mips.resize(mipCount);

            uint32_t w = width, h = height;
            for (uint32_t i = 0; i < mipCount; i++)
            {
                uint64_t mipSize = 0;
                if (!take(&mipSize, 8) || mipSize != MipBytes(image.format, w, h) || position + mipSize > size)
                    return false;
                image.mips[i].assign(data + position, data + position + mipSize);
                position += size_t(mipSize);
                w = std::max(1u, w / 2);
                h = std::max(1u, h / 2);
            }

            out = std::move(image);
            return true;
        }

        bool Load(const std::string& key, CompressedImage& out)
        {
            if (key.empty())
                return false;
            std::ifstream file(key, std::ios::binary | std::ios::ate);
            if (!file)
                return false;
            const std::streamsize size = file.tellg();
            if (size <= 0)
                return false;
            file.seekg(0);
            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
                return false;
            return Deserialize(bytes.data(), bytes.size(), out);
        }

        void Store(const std::string& key, const CompressedImage& image)
        {
            if (key.empty())
                return;

            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(key).parent_path(), ec);

            // Written aside then renamed : another run (or thread) never sees half a file
            static std::atomic<uint64_t> counter{0};
            const std::string temp = key + "." + std::to_string(counter++) + ".tmp";
            {
                std::ofstream file(temp, std::ios::binary | std::ios::trunc);
                if (!file)
                    return;
                const std::vector<uint8_t> bytes = Serialize(image);
                file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                if (!file)
                {
                    file.close();
                    std::filesystem::remove(temp, ec);
                    return;
                }
            }
            std::filesystem::rename(temp, key, ec);
            if (ec)
                std::filesystem::remove(temp, ec);
        }
    }
}

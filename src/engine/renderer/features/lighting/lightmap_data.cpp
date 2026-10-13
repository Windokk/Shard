#include "lightmap_data.hpp"
#include "engine/assets/hot_reloader.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <type_traits>

#include "engine/core/diagnostics/logger.hpp"
#include "engine/core/math/rgb9e5.hpp"

namespace Shard::Engine::Rendering {

    namespace {

        constexpr char kMagic[4] = { 'S', 'H', 'L', 'M' };
        constexpr uint32_t kVersion = 1;

        // A corrupt header must not be able to make the decoder allocate gigabytes : 16384 is the usual GL max texture size
        constexpr uint32_t kMaxPageSize = 16384;
        constexpr uint32_t kMaxPages = 4096;

        // Followed on disk by `pageCount` pages, each a PageHeader and width * height 32 bit texels
        struct FileHeader
        {
            char magic[4];
            uint32_t version;
            uint32_t pageCount;
            uint32_t reserved;
        };
        static_assert(std::is_trivially_copyable_v<FileHeader>, "FileHeader is written to disk as raw bytes");

        struct PageHeader
        {
            uint32_t width;
            uint32_t height;
        };
        static_assert(std::is_trivially_copyable_v<PageHeader>, "PageHeader is written to disk as raw bytes");
    }

    bool WriteLightmapFile(const Filesystem::Path& path, const LightmapData& data)
    {
        for (const LightmapPageData& page : data.pages)
        {
            if (page.width == 0 || page.height == 0 || page.width > kMaxPageSize || page.height > kMaxPageSize ||
                page.texels.size() != size_t(page.width) * page.height)
            {
                DEBUG_ERROR("Lightmap : refusing to write '" + path.full + "', a page doesn't match its own size.");
                return false;
            }
        }
        if (data.pages.size() > kMaxPages)
        {
            DEBUG_ERROR("Lightmap : refusing to write '" + path.full + "', it has too many pages.");
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path.full).parent_path(), ec);

        // Binary explicitly : Filesystem::Path::WriteFile opens in text mode, which on Windows would rewrite every 0x0A byte
        // of the payload as CRLF
        std::ofstream out(path.full, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            DEBUG_ERROR("Lightmap : couldn't open '" + path.full + "' for writing.");
            return false;
        }

        FileHeader header{};
        std::memcpy(header.magic, kMagic, sizeof(kMagic));
        header.version = kVersion;
        header.pageCount = uint32_t(data.pages.size());
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));

        for (const LightmapPageData& page : data.pages)
        {
            const PageHeader pageHeader{ page.width, page.height };
            out.write(reinterpret_cast<const char*>(&pageHeader), sizeof(pageHeader));
            out.write(reinterpret_cast<const char*>(page.texels.data()), std::streamsize(page.texels.size() * sizeof(uint32_t)));
        }
        out.flush();

        if (!out.good())
        {
            DEBUG_ERROR("Lightmap : error while writing '" + path.full + "'.");
            return false;
        }

        out.close();
        Assets::NoteOwnWrite(path.full);        // the bake's own file : not an outside edit for the hot reload
        return true;
    }

    std::shared_ptr<LightmapData> DecodeLightmapFile(const Filesystem::Path& path)
    {
        std::ifstream in(path.full, std::ios::binary | std::ios::ate);
        if (!in.is_open())
        {
            DEBUG_ERROR("Lightmap : couldn't open '" + path.full + "'.");
            return nullptr;
        }

        const uint64_t fileSize = uint64_t(in.tellg());
        in.seekg(0, std::ios::beg);

        FileHeader header{};
        if (fileSize < sizeof(header) || !in.read(reinterpret_cast<char*>(&header), sizeof(header)))
        {
            DEBUG_ERROR("Lightmap : '" + path.full + "' is too small to be a lightmap file.");
            return nullptr;
        }

        if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0)
        {
            DEBUG_ERROR("Lightmap : '" + path.full + "' is not a lightmap file.");
            return nullptr;
        }

        if (header.version != kVersion)
        {
            DEBUG_ERROR("Lightmap : '" + path.full + "' is version " + std::to_string(header.version) +
                        ", this build reads version " + std::to_string(kVersion) + " - bake the lightmaps again.");
            return nullptr;
        }

        if (header.pageCount > kMaxPages)
        {
            DEBUG_ERROR("Lightmap : '" + path.full + "' has an inconsistent header.");
            return nullptr;
        }

        auto data = std::make_shared<LightmapData>();
        data->pages.reserve(header.pageCount);

        uint64_t consumed = sizeof(header);
        for (uint32_t i = 0; i < header.pageCount; i++)
        {
            PageHeader pageHeader{};
            if (consumed + sizeof(pageHeader) > fileSize || !in.read(reinterpret_cast<char*>(&pageHeader), sizeof(pageHeader)))
            {
                DEBUG_ERROR("Lightmap : '" + path.full + "' is truncated.");
                return nullptr;
            }
            consumed += sizeof(pageHeader);

            if (pageHeader.width == 0 || pageHeader.height == 0 || pageHeader.width > kMaxPageSize || pageHeader.height > kMaxPageSize)
            {
                DEBUG_ERROR("Lightmap : '" + path.full + "' has a page of an impossible size.");
                return nullptr;
            }

            const uint64_t payload = uint64_t(pageHeader.width) * pageHeader.height * sizeof(uint32_t);
            if (consumed + payload > fileSize)
            {
                DEBUG_ERROR("Lightmap : '" + path.full + "' is truncated.");
                return nullptr;
            }

            LightmapPageData page;
            page.width = pageHeader.width;
            page.height = pageHeader.height;
            page.texels.resize(size_t(pageHeader.width) * pageHeader.height);
            if (!in.read(reinterpret_cast<char*>(page.texels.data()), std::streamsize(payload)))
            {
                DEBUG_ERROR("Lightmap : read error on '" + path.full + "'.");
                return nullptr;
            }
            consumed += payload;
            data->pages.push_back(std::move(page));
        }

        if (consumed != fileSize)
        {
            DEBUG_ERROR("Lightmap : '" + path.full + "' is " + std::to_string(fileSize) + " bytes, expected " +
                        std::to_string(consumed) + " (corrupt).");
            return nullptr;
        }

        return data;
    }

    LightmapPageData EncodeLightmapPage(uint32_t width, uint32_t height, const float* rgba, int gutterPasses)
    {
        LightmapPageData page;
        page.width = width;
        page.height = height;
        page.texels.assign(size_t(width) * height, 0u);

        const size_t texelCount = size_t(width) * height;
        std::vector<float> color(texelCount * 3);
        std::vector<uint8_t> covered(texelCount);
        for (size_t i = 0; i < texelCount; i++)
        {
            covered[i] = rgba[i * 4 + 3] > 0.5f ? 1 : 0;
            for (int c = 0; c < 3; c++)
            {
                const float v = rgba[i * 4 + c];
                color[i * 3 + c] = (covered[i] && std::isfinite(v)) ? std::max(v, 0.0f) : 0.0f;
            }
        }

        // The gutter : each pass gives the texels next to a covered one the average of the covered ones around them
        std::vector<float> nextColor;
        std::vector<uint8_t> nextCovered;
        for (int pass = 0; pass < gutterPasses; pass++)
        {
            nextColor = color;
            nextCovered = covered;
            bool grew = false;

            for (uint32_t y = 0; y < height; y++)
            {
                for (uint32_t x = 0; x < width; x++)
                {
                    const size_t index = size_t(y) * width + x;
                    if (covered[index])
                        continue;

                    float sum[3] = { 0.0f, 0.0f, 0.0f };
                    int neighbours = 0;
                    for (int dy = -1; dy <= 1; dy++)
                    {
                        for (int dx = -1; dx <= 1; dx++)
                        {
                            const int nx = int(x) + dx, ny = int(y) + dy;
                            if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= int(width) || ny >= int(height))
                                continue;
                            const size_t n = size_t(ny) * width + size_t(nx);
                            if (!covered[n])
                                continue;
                            for (int c = 0; c < 3; c++)
                                sum[c] += color[n * 3 + c];
                            neighbours++;
                        }
                    }

                    if (neighbours > 0)
                    {
                        for (int c = 0; c < 3; c++)
                            nextColor[index * 3 + c] = sum[c] / float(neighbours);
                        nextCovered[index] = 1;
                        grew = true;
                    }
                }
            }

            color.swap(nextColor);
            covered.swap(nextCovered);
            if (!grew)
                break;
        }

        for (size_t i = 0; i < texelCount; i++)
            page.texels[i] = covered[i] ? Core::PackRGB9E5(color[i * 3], color[i * 3 + 1], color[i * 3 + 2]) : 0u;

        return page;
    }
}

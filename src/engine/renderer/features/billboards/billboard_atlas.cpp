#include "billboard_atlas.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <filesystem>

#include "engine/core/diagnostics/logger.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"

namespace Shard::Engine::Rendering {

    namespace {
        // ---- built-in icons : signed distances, in pixels, of a few shapes (negative inside) ----

        float Circle(glm::vec2 p, glm::vec2 c, float r) { return glm::length(p - c) - r; }

        float Segment(glm::vec2 p, glm::vec2 a, glm::vec2 b, float halfWidth)
        {
            const glm::vec2 pa = p - a, ba = b - a;
            const float h = glm::clamp(glm::dot(pa, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
            return glm::length(pa - ba * h) - halfWidth;
        }

        float Box(glm::vec2 p, glm::vec2 c, glm::vec2 halfSize, float radius)
        {
            const glm::vec2 q = glm::abs(p - c) - halfSize + radius;
            return glm::length(glm::max(q, glm::vec2(0.0f))) + std::min(std::max(q.x, q.y), 0.0f) - radius;
        }

        // A convex triangle (counter clockwise) : the largest of the three signed edge distances
        float Triangle(glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c)
        {
            auto edge = [&](glm::vec2 u, glm::vec2 v) {
                const glm::vec2 e = glm::normalize(v - u);
                return glm::dot(p - u, glm::vec2(e.y, -e.x));
            };
            return std::max(edge(a, b), std::max(edge(b, c), edge(c, a)));
        }

        float Ring(glm::vec2 p, glm::vec2 c, float r, float halfWidth) { return std::abs(glm::length(p - c) - r) - halfWidth; }

        // The distance of the glyph of an icon, in a 64 x 64 box (y up)
        float IconDistance(BuiltinSprite sprite, glm::vec2 p)
        {
            const glm::vec2 centre(32.0f, 32.0f);
            switch (sprite)
            {
                case BuiltinSprite::Light:
                {
                    // A bulb : the disc and eight rays around it
                    float d = Circle(p, centre, 10.0f);
                    for (int i = 0; i < 8; i++)
                    {
                        const float a = float(i) * 0.7853982f;
                        const glm::vec2 dir(std::cos(a), std::sin(a));
                        d = std::min(d, Segment(p, centre + dir * 16.0f, centre + dir * 26.0f, 2.2f));
                    }
                    return d;
                }
                case BuiltinSprite::Camera:
                {
                    // A body, a lens cone on its right and a lump on top
                    float d = Box(p, glm::vec2(26.0f, 30.0f), glm::vec2(17.0f, 12.0f), 3.0f);
                    d = std::min(d, Triangle(p, glm::vec2(43.0f, 30.0f), glm::vec2(58.0f, 41.0f), glm::vec2(58.0f, 19.0f)));
                    d = std::min(d, Circle(p, glm::vec2(20.0f, 46.0f), 6.0f));
                    d = std::min(d, Circle(p, glm::vec2(34.0f, 46.0f), 6.0f));
                    return d;
                }
                case BuiltinSprite::ProbeVolume:
                {
                    // A probe : a ring with a dot, and the grid points around it
                    float d = std::min(Ring(p, centre, 15.0f, 2.0f), Circle(p, centre, 5.0f));
                    for (int i = 0; i < 4; i++)
                    {
                        const float a = 0.7853982f + float(i) * 1.5707964f;
                        d = std::min(d, Circle(p, centre + glm::vec2(std::cos(a), std::sin(a)) * 25.0f, 3.5f));
                    }
                    return d;
                }
                default:
                    return std::min(Ring(p, centre, 17.0f, 3.0f), Circle(p, centre, 6.0f));
            }
        }

        glm::vec3 IconColour(BuiltinSprite sprite)
        {
            switch (sprite)
            {
                case BuiltinSprite::Light:       return glm::vec3(1.00f, 0.86f, 0.30f);
                case BuiltinSprite::Camera:      return glm::vec3(0.62f, 0.78f, 1.00f);
                case BuiltinSprite::ProbeVolume: return glm::vec3(0.45f, 0.95f, 0.70f);
                default:                         return glm::vec3(1.0f);
            }
        }

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
            return s;
        }

        // Box filter of a w x h RGBA image (rows `stride` pixels apart) down to one tile. Colours are weighted by alpha so
        // transparent pixels do not darken the edges.
        std::vector<uint8_t> ResampleToTile(const uint8_t* src, uint32_t w, uint32_t h, uint32_t stride)
        {
            constexpr uint32_t T = BillboardAtlas::kTileSize;
            std::vector<uint8_t> out(size_t(T) * T * 4, 0);
            for (uint32_t ty = 0; ty < T; ty++)
            {
                const uint32_t y0 = std::min(ty * h / T, h - 1), y1 = std::min(std::max(y0 + 1, (ty + 1) * h / T), h);
                for (uint32_t tx = 0; tx < T; tx++)
                {
                    const uint32_t x0 = std::min(tx * w / T, w - 1), x1 = std::min(std::max(x0 + 1, (tx + 1) * w / T), w);
                    float r = 0, g = 0, b = 0, a = 0;
                    for (uint32_t y = y0; y < y1; y++)
                    {
                        for (uint32_t x = x0; x < x1; x++)
                        {
                            const uint8_t* p = src + (size_t(y) * stride + x) * 4;
                            const float pa = float(p[3]);
                            r += p[0] * pa; g += p[1] * pa; b += p[2] * pa; a += pa;
                        }
                    }
                    const float count = float((y1 - y0) * (x1 - x0));
                    uint8_t* o = &out[(size_t(ty) * T + tx) * 4];
                    if (a > 0.0f)
                    {
                        o[0] = uint8_t(r / a + 0.5f);
                        o[1] = uint8_t(g / a + 0.5f);
                        o[2] = uint8_t(b / a + 0.5f);
                        o[3] = uint8_t(a / count + 0.5f);
                    }
                }
            }
            return out;
        }
    }

    std::vector<uint8_t> BillboardAtlas::MakeBuiltinPixels(BuiltinSprite sprite, uint32_t size)
    {
        std::vector<uint8_t> pixels(size_t(size) * size * 4, 0);
        const glm::vec3 fill = IconColour(sprite);
        const glm::vec3 outline(0.06f, 0.07f, 0.09f);
        const float scale = 64.0f / float(size);

        for (uint32_t y = 0; y < size; y++)
        {
            for (uint32_t x = 0; x < size; x++)
            {
                // Row 0 is the bottom of the image, like the decoded files
                const glm::vec2 p((float(x) + 0.5f) * scale, (float(y) + 0.5f) * scale);
                const float d = IconDistance(sprite, p) / scale;

                const float inside = glm::clamp(0.5f - d, 0.0f, 1.0f);            // 1 inside the glyph, a pixel of antialiasing
                const float halo = glm::clamp(0.5f + 2.0f / scale - d, 0.0f, 1.0f);   // the dark border around it (2.5 px at 64)

                const glm::vec3 colour = glm::mix(outline, fill, inside);
                uint8_t* out = &pixels[(size_t(y) * size + x) * 4];
                out[0] = uint8_t(colour.r * 255.0f + 0.5f);
                out[1] = uint8_t(colour.g * 255.0f + 0.5f);
                out[2] = uint8_t(colour.b * 255.0f + 0.5f);
                out[3] = uint8_t(halo * 255.0f + 0.5f);
            }
        }
        return pixels;
    }

    BillboardAtlas& BillboardAtlas::Get()
    {
        static BillboardAtlas atlas;
        return atlas;
    }

    BillboardAtlas::BillboardAtlas()
    {
        // The built-in icons come first : their ids are the BuiltinSprite values
        for (uint32_t i = 0; i < static_cast<uint32_t>(BuiltinSprite::Count); i++)
        {
            Sprite sprite;
            sprite.rgba = MakeBuiltinPixels(static_cast<BuiltinSprite>(i), kBuiltinSize);
            m_Sprites.push_back(std::move(sprite));
        }
    }

    uint32_t BillboardAtlas::RegisterSheet(const std::string& key, uint32_t frames, uint32_t width, uint32_t height, const uint8_t* rgba)
    {
        if (frames == 0 || width < frames || height == 0 || !rgba)
            return GetBuiltin(BuiltinSprite::Generic);

        std::lock_guard<std::mutex> lock(m_Mutex);

        const std::string sheetKey = key + "#" + std::to_string(frames);
        auto found = m_Sheets.find(sheetKey);
        if (found != m_Sheets.end())
            return found->second;

        if (m_Sprites.size() + frames > kMaxSprites)
        {
            DEBUG_ERROR("BillboardAtlas : the " + std::to_string(kMaxSprites) + " tiles of the spritesheet are used, '" + key + "' is not added.");
            return GetBuiltin(BuiltinSprite::Generic);
        }

        const uint32_t first = static_cast<uint32_t>(m_Sprites.size());
        const uint32_t cell = width / frames;

        for (uint32_t frame = 0; frame < frames; frame++)
        {
            Sprite sprite;
            sprite.rgba = ResampleToTile(rgba + size_t(frame) * cell * 4, cell, height, width);
            m_Sprites.push_back(std::move(sprite));
        }

        m_Sheets.emplace(sheetKey, first);
        return first;
    }

    uint32_t BillboardAtlas::LoadFolder(const std::string& folder)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_directory(folder, ec))
            return 0;

        std::vector<fs::path> files;
        for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec))
        {
            const std::string ext = Lower(entry.path().extension().string());
            if (entry.is_regular_file() && (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp"))
                files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());     // the same ids on every run

        uint32_t added = 0;
        for (const fs::path& file : files)
        {
            const uint32_t before = GetSpriteCount();
            const uint32_t id = RegisterFile(Filesystem::Path(file.generic_string()), 1);
            if (id == GetBuiltin(BuiltinSprite::Generic))
                continue;       // unreadable, or the atlas is full

            std::lock_guard<std::mutex> lock(m_Mutex);
            const std::string name = Lower(file.stem().string());
            m_Names[name] = id;

            // A file named like a built-in icon replaces its generated glyph (the id stays, so the extractor needs no change)
            static const char* const kBuiltinNames[] = { "light", "camera", "probevolume", "generic" };
            for (uint32_t i = 0; i < static_cast<uint32_t>(BuiltinSprite::Count); i++)
            {
                if (name == kBuiltinNames[i] && i != id)
                {
                    m_Sprites[i].rgba = m_Sprites[id].rgba;
                    m_BuiltSprites = 0;     // the texture must be rebuilt
                }
            }
            added += m_Sprites.size() > before ? 1 : 0;
        }
        return added;
    }

    uint32_t BillboardAtlas::Find(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto found = m_Names.find(Lower(name));
        return found != m_Names.end() ? found->second : static_cast<uint32_t>(BuiltinSprite::Generic);
    }

    uint32_t BillboardAtlas::RegisterFile(const Filesystem::Path& file, uint32_t frames)
    {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto found = m_Sheets.find(file.full + "#" + std::to_string(std::max(frames, 1u)));
            if (found != m_Sheets.end())
                return found->second;
        }

        const TextureDecodeResult decoded = DecodeTextureFile(file);
        if (!decoded.success || decoded.width <= 0 || decoded.height <= 0)
            return GetBuiltin(BuiltinSprite::Generic);

        const size_t pixelCount = size_t(decoded.width) * size_t(decoded.height);
        const size_t channels = decoded.pixels.size() / pixelCount;
        std::vector<uint8_t> rgba(pixelCount * 4, 255);
        for (size_t i = 0; i < pixelCount; i++)
        {
            const uint8_t* src = &decoded.pixels[i * channels];
            uint8_t* dst = &rgba[i * 4];
            switch (channels)
            {
                case 1: dst[0] = dst[1] = dst[2] = src[0]; break;
                case 2: dst[0] = dst[1] = dst[2] = src[0]; dst[3] = src[1]; break;
                case 3: dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; break;
                default: std::memcpy(dst, src, 4); break;
            }
        }

        return RegisterSheet(file.full, std::max(frames, 1u), uint32_t(decoded.width), uint32_t(decoded.height), rgba.data());
    }

    uint32_t BillboardAtlas::GetSpriteCount() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return static_cast<uint32_t>(m_Sprites.size());
    }

    glm::vec4 BillboardAtlas::GetUV(uint32_t sprite) const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (sprite >= m_BuiltSprites)
            sprite = std::min<uint32_t>(static_cast<uint32_t>(BuiltinSprite::Generic), m_BuiltSprites ? m_BuiltSprites - 1 : 0);
        return sprite < m_Sprites.size() ? m_Sprites[sprite].uv : glm::vec4(0.0f);
    }

    std::shared_ptr<Texture2D> BillboardAtlas::GetTexture() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Texture;
    }

    uint64_t BillboardAtlas::GetVersion() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Version;
    }

    bool BillboardAtlas::Rebuild()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (m_Texture && m_BuiltSprites == m_Sprites.size())
            return false;

        std::vector<uint8_t> sheet(size_t(kSheetSize) * kSheetSize * 4, 0);
        const float side = float(kSheetSize);
        const float inset = 0.5f;       // half a texel : bilinear sampling at a tile's border must not reach its neighbour

        for (size_t i = 0; i < m_Sprites.size(); i++)
        {
            Sprite& sprite = m_Sprites[i];
            const uint32_t x = (uint32_t(i) % kTilesPerRow) * kTileSize;
            const uint32_t y = (uint32_t(i) / kTilesPerRow) * kTileSize;

            for (uint32_t row = 0; row < kTileSize; row++)
                std::memcpy(&sheet[(size_t(y + row) * kSheetSize + x) * 4], &sprite.rgba[size_t(row) * kTileSize * 4], size_t(kTileSize) * 4);

            sprite.uv = glm::vec4((float(x) + inset) / side, (float(y) + inset) / side,
                                  (float(x + kTileSize) - inset) / side, (float(y + kTileSize) - inset) / side);
        }

        TextureSpecifications specs;
        specs.width = kSheetSize;
        specs.height = kSheetSize;
        specs.format = TextureFormat::RGBA;
        specs.internalFormat = TextureInternalFormat::RGBA8;
        specs.minFilter = TextureFilter::Linear;
        specs.magFilter = TextureFilter::Linear;
        specs.wrapS = TextureWrap::ClampEdge;
        specs.wrapT = TextureWrap::ClampEdge;
        specs.generateMips = false;

        m_Texture = Texture2D::Create(specs, sheet.data());
        m_BuiltSprites = static_cast<uint32_t>(m_Sprites.size());
        m_Version++;
        return m_Texture != nullptr;
    }
}

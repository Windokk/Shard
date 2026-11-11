#include "texture_settings.hpp"

#include "engine/renderer/rhi/resources/texture/texture_compression.hpp"
#include "engine/core/diagnostics/logger.hpp"

#include <stb/stb_image.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace Shard::Engine::Rendering {

    namespace {

        struct FilterName { TextureFilter value; const char* name; };
        constexpr FilterName kFilters[] = {
            { TextureFilter::Nearest, "Nearest" },
            { TextureFilter::Linear, "Linear" },
            { TextureFilter::NearestMipmapNearest, "NearestMipmapNearest" },
            { TextureFilter::NearestMipmapLinear, "NearestMipmapLinear" },
            { TextureFilter::LinearMipmapNearest, "LinearMipmapNearest" },
            { TextureFilter::LinearMipmapLinear, "LinearMipmapLinear" },
        };

        struct WrapName { TextureWrap value; const char* name; };
        constexpr WrapName kWraps[] = {
            { TextureWrap::Repeat, "Repeat" },
            { TextureWrap::ClampEdge, "ClampEdge" },
            { TextureWrap::ClampBorder, "ClampBorder" },
            { TextureWrap::Mirror, "Mirror" },
        };

        struct SizeModeName { TextureSizeMode value; const char* name; };
        constexpr SizeModeName kSizeModes[] = {
            { TextureSizeMode::Original, "Original" },
            { TextureSizeMode::NearestPowerOfTwo, "NearestPowerOfTwo" },
            { TextureSizeMode::NextPowerOfTwo, "NextPowerOfTwo" },
            { TextureSizeMode::PreviousPowerOfTwo, "PreviousPowerOfTwo" },
            { TextureSizeMode::Custom, "Custom" },
        };

        TextureFilter ParseFilter(const std::string& text, TextureFilter fallback)
        {
            for (const FilterName& f : kFilters)
                if (text == f.name) return f.value;
            return fallback;
        }

        TextureWrap ParseWrap(const std::string& text, TextureWrap fallback)
        {
            for (const WrapName& w : kWraps)
                if (text == w.name) return w.value;
            return fallback;
        }

        TextureSizeMode ParseSizeMode(const std::string& text, TextureSizeMode fallback)
        {
            for (const SizeModeName& m : kSizeModes)
                if (text == m.name) return m.value;
            return fallback;
        }

        bool UsesMips(TextureFilter filter)
        {
            return filter == TextureFilter::LinearMipmapLinear || filter == TextureFilter::LinearMipmapNearest
                || filter == TextureFilter::NearestMipmapLinear || filter == TextureFilter::NearestMipmapNearest;
        }

        TextureFilter WithoutMips(TextureFilter filter)
        {
            switch (filter)
            {
                case TextureFilter::LinearMipmapLinear:
                case TextureFilter::LinearMipmapNearest:   return TextureFilter::Linear;
                case TextureFilter::NearestMipmapLinear:
                case TextureFilter::NearestMipmapNearest:  return TextureFilter::Nearest;
                default:                                   return filter;
            }
        }

        uint64_t Mix(uint64_t hash, uint64_t value)
        {
            hash ^= value;
            hash *= 1099511628211ull;
            return hash;
        }

        constexpr uint32_t kMaxTextureSide = 16384;
    }

    bool TextureImportSettings::operator==(const TextureImportSettings& other) const
    {
        return sizeMode == other.sizeMode && customWidth == other.customWidth && customHeight == other.customHeight
            && maxSize == other.maxSize && compress == other.compress && generateMips == other.generateMips
            && minFilter == other.minFilter && magFilter == other.magFilter && wrapS == other.wrapS && wrapT == other.wrapT;
    }

    uint64_t TextureImportSettings::PixelHash() const
    {
        uint64_t hash = 14695981039346656037ull;
        hash = Mix(hash, uint64_t(sizeMode));
        hash = Mix(hash, sizeMode == TextureSizeMode::Custom ? customWidth : 0);
        hash = Mix(hash, sizeMode == TextureSizeMode::Custom ? customHeight : 0);
        hash = Mix(hash, maxSize);
        hash = Mix(hash, compress ? 1 : 0);
        return hash;
    }

    bool IsTextureSettingsFile(const std::string& path)
    {
        const size_t length = std::strlen(kTextureSettingsExtension);
        if (path.size() <= length)
            return false;
        for (size_t i = 0; i < length; i++)
            if (std::tolower(static_cast<unsigned char>(path[path.size() - length + i])) != kTextureSettingsExtension[i])
                return false;
        return true;
    }

    std::string ImagePathOfSettingsFile(const std::string& path)
    {
        if (!IsTextureSettingsFile(path))
            return "";
        return path.substr(0, path.size() - std::strlen(kTextureSettingsExtension));
    }

    Filesystem::Path TextureSettingsPath(const Filesystem::Path& image)
    {
        return Filesystem::Path(image.full + kTextureSettingsExtension);
    }

    TextureImportSettings LoadTextureSettings(const Filesystem::Path& image)
    {
        TextureImportSettings settings;

        const Filesystem::Path file = TextureSettingsPath(image);
        if (!file.Exists())
            return settings;

        nlohmann::json data = nlohmann::json::parse(file.ReadFile(), nullptr, false);
        if (data.is_discarded() || !data.is_object())
        {
            DEBUG_WARNING("Texture settings of \"" + image.full + "\" are not valid : using the defaults");
            return settings;
        }

        auto text = [&](const char* key) { return data.contains(key) && data[key].is_string() ? data[key].get<std::string>() : std::string(); };
        auto number = [&](const char* key, uint32_t fallback) {
            return data.contains(key) && data[key].is_number_integer() ? uint32_t(std::clamp<int64_t>(data[key].get<int64_t>(), 0, kMaxTextureSide)) : fallback;
        };
        auto flag = [&](const char* key, bool fallback) { return data.contains(key) && data[key].is_boolean() ? data[key].get<bool>() : fallback; };

        settings.sizeMode = ParseSizeMode(text("sizeMode"), settings.sizeMode);
        settings.customWidth = number("customWidth", settings.customWidth);
        settings.customHeight = number("customHeight", settings.customHeight);
        settings.maxSize = number("maxSize", settings.maxSize);
        settings.compress = flag("compress", settings.compress);
        settings.generateMips = flag("generateMips", settings.generateMips);
        settings.minFilter = ParseFilter(text("minFilter"), settings.minFilter);
        settings.magFilter = ParseFilter(text("magFilter"), settings.magFilter);
        settings.wrapS = ParseWrap(text("wrapS"), settings.wrapS);
        settings.wrapT = ParseWrap(text("wrapT"), settings.wrapT);
        return settings;
    }

    bool SaveTextureSettings(const Filesystem::Path& image, const TextureImportSettings& settings)
    {
        const Filesystem::Path file = TextureSettingsPath(image);

        if (settings == TextureImportSettings())
        {
            std::error_code ec;
            std::filesystem::remove(file.GetNativePath(), ec);
            return !ec;
        }

        nlohmann::json data;
        data["sizeMode"] = TextureSizeModeName(settings.sizeMode);
        data["customWidth"] = settings.customWidth;
        data["customHeight"] = settings.customHeight;
        data["maxSize"] = settings.maxSize;
        data["compress"] = settings.compress;
        data["generateMips"] = settings.generateMips;
        data["minFilter"] = TextureFilterName(settings.minFilter);
        data["magFilter"] = TextureFilterName(settings.magFilter);
        data["wrapS"] = TextureWrapName(settings.wrapS);
        data["wrapT"] = TextureWrapName(settings.wrapT);
        return file.WriteFile(data.dump(4));
    }

    void ApplyTextureSettings(const TextureImportSettings& settings, TextureSpecifications& specs)
    {
        specs.generateMips = settings.generateMips;
        specs.minFilter = settings.generateMips ? settings.minFilter : WithoutMips(settings.minFilter);
        specs.magFilter = WithoutMips(settings.magFilter);
        specs.wrapS = settings.wrapS;
        specs.wrapT = settings.wrapT;
    }

    bool IsPowerOfTwo(uint32_t value)
    {
        return value != 0 && (value & (value - 1)) == 0;
    }

    uint32_t NextPowerOfTwo(uint32_t value)
    {
        if (value <= 1)
            return 1;
        uint32_t result = 1;
        while (result < value && result < (1u << 31))
            result <<= 1;
        return result;
    }

    uint32_t PreviousPowerOfTwo(uint32_t value)
    {
        if (value <= 1)
            return 1;
        uint32_t result = 1;
        while ((result << 1) <= value && (result << 1) != 0)
            result <<= 1;
        return result;
    }

    uint32_t NearestPowerOfTwo(uint32_t value)
    {
        const uint32_t below = PreviousPowerOfTwo(value);
        const uint32_t above = NextPowerOfTwo(value);
        return (value - below) < (above - value) ? below : above;
    }

    void ResolveTextureSize(const TextureImportSettings& settings, uint32_t width, uint32_t height, uint32_t& outWidth, uint32_t& outHeight)
    {
        outWidth = width;
        outHeight = height;

        switch (settings.sizeMode)
        {
            case TextureSizeMode::Original:
                break;
            case TextureSizeMode::NearestPowerOfTwo:
                outWidth = NearestPowerOfTwo(width);
                outHeight = NearestPowerOfTwo(height);
                break;
            case TextureSizeMode::NextPowerOfTwo:
                outWidth = NextPowerOfTwo(width);
                outHeight = NextPowerOfTwo(height);
                break;
            case TextureSizeMode::PreviousPowerOfTwo:
                outWidth = PreviousPowerOfTwo(width);
                outHeight = PreviousPowerOfTwo(height);
                break;
            case TextureSizeMode::Custom:
                outWidth = settings.customWidth > 0 ? settings.customWidth : width;
                outHeight = settings.customHeight > 0 ? settings.customHeight : height;
                break;
        }

        if (settings.maxSize > 0)
        {
            const uint32_t side = std::max(outWidth, outHeight);
            if (side > settings.maxSize)
            {
                const double scale = double(settings.maxSize) / double(side);
                outWidth = std::max(1u, uint32_t(std::lround(outWidth * scale)));
                outHeight = std::max(1u, uint32_t(std::lround(outHeight * scale)));
            }
        }

        outWidth = std::clamp(outWidth, 1u, kMaxTextureSide);
        outHeight = std::clamp(outHeight, 1u, kMaxTextureSide);
    }

    namespace {

        struct Tap { uint32_t first = 0; std::vector<float> weights; };

        std::vector<Tap> MakeTaps(uint32_t sourceSize, uint32_t targetSize)
        {
            const double scale = double(sourceSize) / double(targetSize);
            const double radius = std::max(1.0, scale);

            std::vector<Tap> taps(targetSize);
            for (uint32_t i = 0; i < targetSize; i++)
            {
                const double center = (double(i) + 0.5) * scale - 0.5;
                const int64_t low = std::max<int64_t>(0, int64_t(std::floor(center - radius)) + 1);
                const int64_t high = std::min<int64_t>(int64_t(sourceSize) - 1, int64_t(std::ceil(center + radius)) - 1);

                Tap& tap = taps[i];
                tap.first = uint32_t(low);

                double sum = 0.0;
                for (int64_t s = low; s <= high; s++)
                {
                    const double weight = std::max(0.0, 1.0 - std::abs(double(s) - center) / radius);
                    tap.weights.push_back(float(weight));
                    sum += weight;
                }

                if (tap.weights.empty() || sum <= 0.0)
                {
                    tap.first = uint32_t(std::clamp<int64_t>(int64_t(std::lround(center)), 0, int64_t(sourceSize) - 1));
                    tap.weights.assign(1, 1.0f);
                    continue;
                }
                for (float& weight : tap.weights)
                    weight = float(weight / sum);
            }
            return taps;
        }
    }

    bool ResizeImage(std::vector<unsigned char>& pixels, uint32_t width, uint32_t height, uint32_t channels, uint32_t newWidth, uint32_t newHeight)
    {
        if (width == 0 || height == 0 || newWidth == 0 || newHeight == 0 || channels == 0 || channels > 4)
            return false;
        if (pixels.size() != size_t(width) * height * channels)
            return false;
        if (width == newWidth && height == newHeight)
            return true;

        const std::vector<Tap> columns = MakeTaps(width, newWidth);
        const std::vector<Tap> rows = MakeTaps(height, newHeight);

        std::vector<float> horizontal(size_t(newWidth) * height * channels);
        for (uint32_t y = 0; y < height; y++)
        {
            const unsigned char* source = pixels.data() + size_t(y) * width * channels;
            float* target = horizontal.data() + size_t(y) * newWidth * channels;
            for (uint32_t x = 0; x < newWidth; x++)
            {
                const Tap& tap = columns[x];
                float sum[4] = { 0, 0, 0, 0 };
                for (size_t k = 0; k < tap.weights.size(); k++)
                    for (uint32_t c = 0; c < channels; c++)
                        sum[c] += tap.weights[k] * float(source[(size_t(tap.first) + k) * channels + c]);
                for (uint32_t c = 0; c < channels; c++)
                    target[size_t(x) * channels + c] = sum[c];
            }
        }

        std::vector<unsigned char> result(size_t(newWidth) * newHeight * channels);
        for (uint32_t y = 0; y < newHeight; y++)
        {
            const Tap& tap = rows[y];
            for (uint32_t x = 0; x < newWidth; x++)
            {
                float sum[4] = { 0, 0, 0, 0 };
                for (size_t k = 0; k < tap.weights.size(); k++)
                    for (uint32_t c = 0; c < channels; c++)
                        sum[c] += tap.weights[k] * horizontal[((size_t(tap.first) + k) * newWidth + x) * channels + c];
                for (uint32_t c = 0; c < channels; c++)
                    result[(size_t(y) * newWidth + x) * channels + c] = static_cast<unsigned char>(std::clamp(sum[c] + 0.5f, 0.0f, 255.0f));
            }
        }

        pixels = std::move(result);
        return true;
    }

    TextureFileInfo ReadTextureFileInfo(const Filesystem::Path& image)
    {
        TextureFileInfo info;
        if (!image.Exists())
            return info;

        const std::string file = image.ReadFile();
        int width = 0, height = 0, channels = 0;
        if (!stbi_info_from_memory(reinterpret_cast<const unsigned char*>(file.data()), static_cast<int>(file.size()), &width, &height, &channels))
            return info;

        info.valid = true;
        info.width = uint32_t(width);
        info.height = uint32_t(height);
        info.channels = uint32_t(channels);
        return info;
    }

    uint64_t EstimateTextureBytes(const TextureImportSettings& settings, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t channels)
    {
        uint32_t width = 0, height = 0;
        ResolveTextureSize(settings, sourceWidth, sourceHeight, width, height);

        const bool compressed = settings.compress && ShouldCompress(width, height) && channels >= 1 && channels <= 4;
        const uint32_t levels = settings.generateMips ? MipCount(width, height) : 1;

        uint64_t total = 0;
        for (uint32_t level = 0; level < levels; level++)
        {
            const uint32_t w = std::max(1u, width >> level);
            const uint32_t h = std::max(1u, height >> level);
            if (compressed)
            {
                const BlockFormat format = channels == 1 ? BlockFormat::BC4 : channels == 2 ? BlockFormat::BC5 : BlockFormat::BC7;
                total += MipBytes(format, w, h);
            }
            else
                total += uint64_t(w) * h * (channels == 3 ? 4 : channels);
        }
        return total;
    }

    const char* TextureFilterName(TextureFilter filter)
    {
        for (const FilterName& f : kFilters)
            if (f.value == filter) return f.name;
        return "Linear";
    }

    const char* TextureWrapName(TextureWrap wrap)
    {
        for (const WrapName& w : kWraps)
            if (w.value == wrap) return w.name;
        return "Repeat";
    }

    const char* TextureSizeModeName(TextureSizeMode mode)
    {
        for (const SizeModeName& m : kSizeModes)
            if (m.value == mode) return m.name;
        return "Original";
    }
}

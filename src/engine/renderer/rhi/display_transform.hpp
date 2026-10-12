#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering {

    // The display transform : how the linear HDR light the renderer computes becomes the 8 bit colours a monitor shows.
    //
    // Lighting is arithmetic on physical quantities, so it only gives the right answers on LINEAR values : textures are
    // linearized as they are sampled (sRGB textures, see TextureSpecifications::srgb), the scene is rendered into a floating
    // point target whose values may be far above 1 (a sun, a lamp, a specular glint), and one last pass maps that range onto
    // what the screen can show - exposure, a tonemapper (an S shaped curve that rolls the highlights off instead of clipping
    // them, as a film does) and the sRGB transfer curve that a monitor expects.
    //
    // This is the C++ twin of shaders/common/tonemap.glsl : every constant and formula below is the same there, and
    // has to stay so (the offline raytrace's preview, the tests and the shader all have to agree).

    /// Mirrored by `tonemapper` in tonemap.frag / lit.frag - keep in sync.
    enum class Tonemapper : int
    {
        /// No curve : clamped to [0, 1] (everything brighter than white is white). Exposure and the sRGB curve still apply.
        None = 0,
        /// The Academy filmic curve (Stephen Hill's fit of the RRT and ODT) : contrasty, desaturates bright colours toward
        /// white. The look of most AAA games and of Unreal Engine.
        ACES = 1,
        /// Khronos PBR Neutral : leaves everything below ~0.8 untouched and only compresses what is brighter, so the colours
        /// of a material stay the colours of the material. Made for product shots.
        Neutral = 2,
        /// Reinhard on the luminance : the simplest soft shoulder, keeps hues.
        Reinhard = 3
    };

    inline const char* TonemapperName(Tonemapper tonemapper)
    {
        switch (tonemapper)
        {
            case Tonemapper::None: return "none";
            case Tonemapper::ACES: return "aces";
            case Tonemapper::Neutral: return "neutral";
            case Tonemapper::Reinhard: return "reinhard";
        }
        return "aces";
    }

    inline bool ParseTonemapper(const std::string& text, Tonemapper& out)
    {
        std::string lower;
        for (char c : text)
            lower.push_back(char(std::tolower(static_cast<unsigned char>(c))));

        for (Tonemapper candidate : { Tonemapper::None, Tonemapper::ACES, Tonemapper::Neutral, Tonemapper::Reinhard })
        {
            if (lower == TonemapperName(candidate))
            {
                out = candidate;
                return true;
            }
        }
        return false;
    }

    /// How a world is shown : the curve, and the exposure in stops (EV : every +1 doubles the light, every -1 halves it)
    struct DisplaySettings
    {
        Tonemapper tonemapper = Tonemapper::ACES;
        float exposure = 0.0f;

        /// The factor the scene's light is multiplied by before the curve
        float ExposureScale() const { return std::exp2(exposure); }

        bool operator==(const DisplaySettings& o) const { return tonemapper == o.tonemapper && exposure == o.exposure; }
        bool operator!=(const DisplaySettings& o) const { return !(*this == o); }
    };

    /// Linear light -> the sRGB transfer curve (IEC 61966-2-1) : what a monitor expects, and what a texture holds
    inline float LinearToSRGB(float linear)
    {
        linear = std::clamp(linear, 0.0f, 1.0f);
        return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    }

    inline float SRGBToLinear(float encoded)
    {
        encoded = std::clamp(encoded, 0.0f, 1.0f);
        return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
    }

    inline glm::vec3 LinearToSRGB(const glm::vec3& c) { return glm::vec3(LinearToSRGB(c.x), LinearToSRGB(c.y), LinearToSRGB(c.z)); }
    inline glm::vec3 SRGBToLinear(const glm::vec3& c) { return glm::vec3(SRGBToLinear(c.x), SRGBToLinear(c.y), SRGBToLinear(c.z)); }

    namespace Detail {

        // Hill's fit of the ACES reference rendering transform and sRGB output transform. The matrices take sRGB to the
        // working space of the curve and back (written here by columns, as GLSL's mat3 constructor takes them).
        inline glm::vec3 RRTAndODTFit(const glm::vec3& v)
        {
            const glm::vec3 a = v * (v + 0.0245786f) - 0.000090537f;
            const glm::vec3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
            return a / b;
        }

        inline glm::vec3 ACESFitted(const glm::vec3& color)
        {
            const glm::mat3 inputMatrix(glm::vec3(0.59719f, 0.07600f, 0.02840f), glm::vec3(0.35458f, 0.90834f, 0.13383f), glm::vec3(0.04823f, 0.01566f, 0.83777f));
            const glm::mat3 outputMatrix(glm::vec3(1.60475f, -0.10208f, -0.00327f), glm::vec3(-0.53108f, 1.10813f, -0.07276f), glm::vec3(-0.07367f, -0.00605f, 1.07602f));
            return glm::clamp(outputMatrix * RRTAndODTFit(inputMatrix * color), 0.0f, 1.0f);
        }

        // Khronos PBR Neutral tone mapping (Apache 2.0, github.com/KhronosGroup/ToneMapping)
        inline glm::vec3 PBRNeutral(glm::vec3 color)
        {
            const float startCompression = 0.8f - 0.04f;
            const float desaturation = 0.15f;

            const float x = std::min(color.r, std::min(color.g, color.b));
            const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
            color -= offset;

            const float peak = std::max(color.r, std::max(color.g, color.b));
            if (peak < startCompression)
                return color;

            const float d = 1.0f - startCompression;
            const float newPeak = 1.0f - d * d / (peak + d - startCompression);
            color *= newPeak / peak;

            const float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
            return glm::mix(color, glm::vec3(newPeak), g);
        }

        inline glm::vec3 ReinhardLuminance(const glm::vec3& color)
        {
            const float luminance = glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
            return color / (1.0f + luminance);
        }
    }

    /// Linear scene light (any range) -> linear light in [0, 1] : the curve alone
    inline glm::vec3 ApplyTonemap(const glm::vec3& color, Tonemapper tonemapper)
    {
        const glm::vec3 c = glm::max(color, glm::vec3(0.0f));
        switch (tonemapper)
        {
            case Tonemapper::None: return glm::clamp(c, 0.0f, 1.0f);
            case Tonemapper::ACES: return Detail::ACESFitted(c);
            case Tonemapper::Neutral: return glm::clamp(Detail::PBRNeutral(c), 0.0f, 1.0f);
            case Tonemapper::Reinhard: return glm::clamp(Detail::ReinhardLuminance(c), 0.0f, 1.0f);
        }
        return glm::clamp(c, 0.0f, 1.0f);
    }

    /// The whole transform of a pixel : exposure, curve, sRGB encoding. The result is what is stored in the 8 bit display target.
    inline glm::vec3 DisplayTransform(const glm::vec3& hdr, const DisplaySettings& settings)
    {
        return LinearToSRGB(ApplyTonemap(hdr * settings.ExposureScale(), settings.tonemapper));
    }
}

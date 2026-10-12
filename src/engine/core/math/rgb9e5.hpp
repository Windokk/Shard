#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Shard::Engine::Core {

    /// @brief Packs a linear HDR colour into the 32 bits of GL_RGB9_E5 : three 9 bit mantissas that share one 5 bit exponent
    /// (what the GPU reads back as a float3 by itself, bilinear filtering included). Half the size of RGBA16F and, unlike
    /// R11G11B10F, 9 bits of precision on each channel : about 0.2 % of the brightest channel, with no banding on a smooth
    /// gradient. Negative values and NaN become 0, values above 65408 are clamped. The conversion is the one of the
    /// EXT_texture_shared_exponent specification.
    inline uint32_t PackRGB9E5(float r, float g, float b)
    {
        constexpr int kMantissaBits = 9;
        constexpr int kExponentBias = 15;
        constexpr int kMaxExponent = 31;
        constexpr float kMaxValue = float((1 << kMantissaBits) - 1) / float(1 << kMantissaBits) * float(1 << (kMaxExponent - kExponentBias));

        auto clampChannel = [&](float c) {
            return (c > 0.0f) ? std::min(c, kMaxValue) : 0.0f;      // NaN is not > 0 : it becomes 0
        };

        r = clampChannel(r);
        g = clampChannel(g);
        b = clampChannel(b);

        const float maxChannel = std::max(r, std::max(g, b));
        if (maxChannel <= 0.0f)
            return 0;

        int exponent = std::max(-kExponentBias - 1, int(std::floor(std::log2(maxChannel)))) + 1 + kExponentBias;
        float denominator = std::exp2(float(exponent - kExponentBias - kMantissaBits));

        // Rounding the largest channel up may carry into the next exponent
        if (int(std::floor(maxChannel / denominator + 0.5f)) == (1 << kMantissaBits))
        {
            exponent++;
            denominator *= 2.0f;
        }

        const uint32_t rm = uint32_t(std::floor(r / denominator + 0.5f));
        const uint32_t gm = uint32_t(std::floor(g / denominator + 0.5f));
        const uint32_t bm = uint32_t(std::floor(b / denominator + 0.5f));
        return (uint32_t(exponent) << 27) | (bm << 18) | (gm << 9) | rm;
    }

    inline void UnpackRGB9E5(uint32_t packed, float& r, float& g, float& b)
    {
        const int exponent = int(packed >> 27);
        const float scale = std::exp2(float(exponent - 15 - 9));
        r = float(packed & 511u) * scale;
        g = float((packed >> 9) & 511u) * scale;
        b = float((packed >> 18) & 511u) * scale;
    }
}

#include "engine/core/math/noise.hpp"

#include <algorithm>
#include <cmath>

#include "engine/core/math/hash.hpp"

namespace Shard::Engine::Core::Noise {

    namespace {
        // Quintic fade (Perlin 2002) : zero first AND second derivative at the lattice points, no visible grid
        inline float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
        inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

        inline float Floor(float v) { return std::floor(v); }

        // [-1, 1] from a hash
        inline float HashToSigned(uint32_t h) { return static_cast<float>(h >> 8) * (2.0f / 16777215.0f) - 1.0f; }
        inline float HashToUnit(uint32_t h) { return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); }

        // 2D gradient : one of 8 directions
        inline float Grad2(uint32_t hash, float x, float y) {
            switch (hash & 7u) {
                case 0: return  x + y;
                case 1: return -x + y;
                case 2: return  x - y;
                case 3: return -x - y;
                case 4: return  x;
                case 5: return -x;
                case 6: return  y;
                default: return -y;
            }
        }

        // 3D gradient : the 12 edge directions of a cube (Perlin's improved noise), 4 entries repeated
        inline float Grad3(uint32_t hash, float x, float y, float z) {
            const uint32_t h = hash & 15u;
            const float u = h < 8 ? x : y;
            const float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
            return ((h & 1u) ? -u : u) + ((h & 2u) ? -v : v);
        }
    }

    float Value(float x, float y, uint32_t seed) {
        const float fx = Floor(x), fy = Floor(y);
        const int32_t ix = static_cast<int32_t>(fx), iy = static_cast<int32_t>(fy);
        const float tx = Fade(x - fx), ty = Fade(y - fy);
        const float v00 = HashToSigned(HashInts(ix, iy, 0, seed));
        const float v10 = HashToSigned(HashInts(ix + 1, iy, 0, seed));
        const float v01 = HashToSigned(HashInts(ix, iy + 1, 0, seed));
        const float v11 = HashToSigned(HashInts(ix + 1, iy + 1, 0, seed));
        return Lerp(Lerp(v00, v10, tx), Lerp(v01, v11, tx), ty);
    }

    float Value3(float x, float y, float z, uint32_t seed) {
        const float fx = Floor(x), fy = Floor(y), fz = Floor(z);
        const int32_t ix = static_cast<int32_t>(fx), iy = static_cast<int32_t>(fy), iz = static_cast<int32_t>(fz);
        const float tx = Fade(x - fx), ty = Fade(y - fy), tz = Fade(z - fz);
        auto corner = [&](int dx, int dy, int dz) { return HashToSigned(HashInts(ix + dx, iy + dy, iz + dz, seed)); };
        const float x00 = Lerp(corner(0, 0, 0), corner(1, 0, 0), tx);
        const float x10 = Lerp(corner(0, 1, 0), corner(1, 1, 0), tx);
        const float x01 = Lerp(corner(0, 0, 1), corner(1, 0, 1), tx);
        const float x11 = Lerp(corner(0, 1, 1), corner(1, 1, 1), tx);
        return Lerp(Lerp(x00, x10, ty), Lerp(x01, x11, ty), tz);
    }

    float Perlin(float x, float y, uint32_t seed) {
        const float fx = Floor(x), fy = Floor(y);
        const int32_t ix = static_cast<int32_t>(fx), iy = static_cast<int32_t>(fy);
        const float dx = x - fx, dy = y - fy;
        const float tx = Fade(dx), ty = Fade(dy);

        const float n00 = Grad2(HashInts(ix, iy, 0, seed), dx, dy);
        const float n10 = Grad2(HashInts(ix + 1, iy, 0, seed), dx - 1.0f, dy);
        const float n01 = Grad2(HashInts(ix, iy + 1, 0, seed), dx, dy - 1.0f);
        const float n11 = Grad2(HashInts(ix + 1, iy + 1, 0, seed), dx - 1.0f, dy - 1.0f);

        // The raw range of this gradient set is about [-1, 1] already (axis-aligned and diagonal gradients)
        return std::clamp(Lerp(Lerp(n00, n10, tx), Lerp(n01, n11, tx), ty), -1.0f, 1.0f);
    }

    float Perlin3(float x, float y, float z, uint32_t seed) {
        const float fx = Floor(x), fy = Floor(y), fz = Floor(z);
        const int32_t ix = static_cast<int32_t>(fx), iy = static_cast<int32_t>(fy), iz = static_cast<int32_t>(fz);
        const float dx = x - fx, dy = y - fy, dz = z - fz;
        const float tx = Fade(dx), ty = Fade(dy), tz = Fade(dz);

        auto g = [&](int ox, int oy, int oz) {
            return Grad3(HashInts(ix + ox, iy + oy, iz + oz, seed), dx - ox, dy - oy, dz - oz);
        };
        const float x00 = Lerp(g(0, 0, 0), g(1, 0, 0), tx);
        const float x10 = Lerp(g(0, 1, 0), g(1, 1, 0), tx);
        const float x01 = Lerp(g(0, 0, 1), g(1, 0, 1), tx);
        const float x11 = Lerp(g(0, 1, 1), g(1, 1, 1), tx);
        return std::clamp(Lerp(Lerp(x00, x10, ty), Lerp(x01, x11, ty), tz), -1.0f, 1.0f);
    }

    float Worley(float x, float y, uint32_t seed) {
        const int32_t ix = static_cast<int32_t>(Floor(x)), iy = static_cast<int32_t>(Floor(y));
        float best = 1e9f;
        for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox) {
                const int32_t cx = ix + ox, cy = iy + oy;
                const uint32_t h = HashInts(cx, cy, 0, seed);
                const float px = static_cast<float>(cx) + HashToUnit(h);
                const float py = static_cast<float>(cy) + HashToUnit(Mix32(h ^ 0x68bc21ebu));
                const float dx = x - px, dy = y - py;
                best = std::min(best, dx * dx + dy * dy);
            }
        return std::sqrt(best);
    }

    float Worley3(float x, float y, float z, uint32_t seed) {
        const int32_t ix = static_cast<int32_t>(Floor(x)), iy = static_cast<int32_t>(Floor(y)), iz = static_cast<int32_t>(Floor(z));
        float best = 1e9f;
        for (int oz = -1; oz <= 1; ++oz)
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox) {
                    const int32_t cx = ix + ox, cy = iy + oy, cz = iz + oz;
                    const uint32_t h = HashInts(cx, cy, cz, seed);
                    const float px = static_cast<float>(cx) + HashToUnit(h);
                    const float py = static_cast<float>(cy) + HashToUnit(Mix32(h ^ 0x68bc21ebu));
                    const float pz = static_cast<float>(cz) + HashToUnit(Mix32(h ^ 0x02e5be93u));
                    const float dx = x - px, dy = y - py, dz = z - pz;
                    best = std::min(best, dx * dx + dy * dy + dz * dz);
                }
        return std::sqrt(best);
    }

    float Fbm(float x, float y, uint32_t seed, const FractalParams& params) {
        float sum = 0.0f, amplitude = 1.0f, frequency = 1.0f, total = 0.0f;
        for (int i = 0; i < params.octaves; ++i) {
            sum += amplitude * Perlin(x * frequency, y * frequency, seed + static_cast<uint32_t>(i) * 0x9e3779b9u);
            total += amplitude;
            amplitude *= params.gain;
            frequency *= params.lacunarity;
        }
        return total > 0.0f ? sum / total : 0.0f;
    }

    float Fbm3(float x, float y, float z, uint32_t seed, const FractalParams& params) {
        float sum = 0.0f, amplitude = 1.0f, frequency = 1.0f, total = 0.0f;
        for (int i = 0; i < params.octaves; ++i) {
            sum += amplitude * Perlin3(x * frequency, y * frequency, z * frequency, seed + static_cast<uint32_t>(i) * 0x9e3779b9u);
            total += amplitude;
            amplitude *= params.gain;
            frequency *= params.lacunarity;
        }
        return total > 0.0f ? sum / total : 0.0f;
    }

    float Ridged(float x, float y, uint32_t seed, const FractalParams& params) {
        float sum = 0.0f, amplitude = 1.0f, frequency = 1.0f, total = 0.0f;
        for (int i = 0; i < params.octaves; ++i) {
            const float n = 1.0f - std::fabs(Perlin(x * frequency, y * frequency, seed + static_cast<uint32_t>(i) * 0x9e3779b9u));
            sum += amplitude * n * n;                    // squared : sharper ridges
            total += amplitude;
            amplitude *= params.gain;
            frequency *= params.lacunarity;
        }
        return total > 0.0f ? sum / total : 0.0f;
    }
}

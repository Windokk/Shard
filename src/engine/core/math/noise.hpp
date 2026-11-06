#pragma once

#include <cstdint>

namespace Shard::Engine::Core::Noise {

    // Pure function of (coords, seed), no tables or global state, same on every platform. Coords in lattice units, results in [-1, 1] unless noted.

    float Value(float x, float y, uint32_t seed = 0);
    float Value3(float x, float y, float z, uint32_t seed = 0);

    float Perlin(float x, float y, uint32_t seed = 0);
    float Perlin3(float x, float y, float z, uint32_t seed = 0);

    float Worley(float x, float y, uint32_t seed = 0);
    float Worley3(float x, float y, float z, uint32_t seed = 0);

    struct FractalParams {
        int octaves = 5;
        float lacunarity = 2.0f;
        float gain = 0.5f;
    };

    float Fbm(float x, float y, uint32_t seed = 0, const FractalParams& params = {});
    float Fbm3(float x, float y, float z, uint32_t seed = 0, const FractalParams& params = {});

    float Ridged(float x, float y, uint32_t seed = 0, const FractalParams& params = {});
}

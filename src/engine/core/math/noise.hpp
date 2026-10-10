#pragma once

#include <cstdint>

namespace Shard::Engine::Core::Noise {

    /// Seeded coherent noise : the value is a pure function of (coordinates, seed), identical on every platform
    /// (the lattice is hashed, no permutation table and no global state), so it can be evaluated from any thread and
    /// regenerates the same terrain from the same seed. Coordinates are in "lattice units" : features are about 1 unit
    /// wide, scale the input to change that.
    ///
    /// All functions return values in [-1, 1] except where noted.

    /// Value noise : random values on the lattice points, smoothly interpolated. Cheap, blocky look.
    float Value(float x, float y, uint32_t seed = 0);
    float Value3(float x, float y, float z, uint32_t seed = 0);

    /// Gradient (Perlin) noise : random gradients on the lattice, smoother and more isotropic than value noise.
    /// Exactly 0 on every lattice point.
    float Perlin(float x, float y, uint32_t seed = 0);
    float Perlin3(float x, float y, float z, uint32_t seed = 0);

    /// Cellular (Worley) noise : distance to the nearest of one random point per cell, in [0, ~1.2]. Cell-like
    /// patterns (stone, cracks, bubbles).
    float Worley(float x, float y, uint32_t seed = 0);
    float Worley3(float x, float y, float z, uint32_t seed = 0);

    struct FractalParams {
        int octaves = 5;
        float lacunarity = 2.0f;        ///< frequency multiplier from one octave to the next
        float gain = 0.5f;              ///< amplitude multiplier from one octave to the next
    };

    /// Fractal Brownian motion : several octaves of Perlin added up, normalised back to [-1, 1].
    float Fbm(float x, float y, uint32_t seed = 0, const FractalParams& params = {});
    float Fbm3(float x, float y, float z, uint32_t seed = 0, const FractalParams& params = {});

    /// Ridged multifractal : sharp ridges (mountains, veins). Returns [0, 1].
    float Ridged(float x, float y, uint32_t seed = 0, const FractalParams& params = {});
}

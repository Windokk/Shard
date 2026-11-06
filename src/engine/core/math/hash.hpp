#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace Shard::Engine::Core {

    // FNV-1a. Not for adversarial inputs, weak on short binary keys (use Mix64 or Murmur3).
    constexpr uint32_t Fnv1a32(std::string_view text, uint32_t seed = 2166136261u) {
        uint32_t hash = seed;
        for (char c : text) { hash ^= static_cast<uint8_t>(c); hash *= 16777619u; }
        return hash;
    }

    constexpr uint64_t Fnv1a64(std::string_view text, uint64_t seed = 14695981039346656037ull) {
        uint64_t hash = seed;
        for (char c : text) { hash ^= static_cast<uint8_t>(c); hash *= 1099511628211ull; }
        return hash;
    }

    inline uint64_t Fnv1a64(const void* data, size_t size, uint64_t seed) {
        return Fnv1a64(std::string_view(static_cast<const char*>(data), size), seed);
    }

    constexpr uint32_t Mix32(uint32_t x) {
        x ^= x >> 16; x *= 0x85ebca6bu;
        x ^= x >> 13; x *= 0xc2b2ae35u;
        x ^= x >> 16;
        return x;
    }

    constexpr uint64_t Mix64(uint64_t x) {
        x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull;
        x ^= x >> 27; x *= 0x94d049bb133111ebull;
        x ^= x >> 31;
        return x;
    }

    constexpr uint64_t HashCombine(uint64_t seed, uint64_t value) {
        return seed ^ (Mix64(value) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
    }

    constexpr uint32_t HashInts(int32_t x, int32_t y, int32_t z, uint32_t seed) {
        uint32_t h = Mix32(seed ^ 0x9e3779b9u);
        h = Mix32(h ^ static_cast<uint32_t>(x) * 0x27d4eb2fu);
        h = Mix32(h ^ static_cast<uint32_t>(y) * 0x165667b1u);
        h = Mix32(h ^ static_cast<uint32_t>(z) * 0x85ebca77u);
        return h;
    }

    // MurmurHash3 x86 32, reference results. No default seed: Murmur3_32("text", seed) would read `seed` bytes.
    inline uint32_t Murmur3_32(const void* data, size_t size, uint32_t seed) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        const size_t blocks = size / 4;
        constexpr uint32_t c1 = 0xcc9e2d51u, c2 = 0x1b873593u;
        auto rotl = [](uint32_t v, int r) { return (v << r) | (v >> (32 - r)); };

        uint32_t h = seed;
        for (size_t i = 0; i < blocks; ++i) {
            uint32_t k;
            std::memcpy(&k, bytes + i * 4, 4);
            k *= c1; k = rotl(k, 15); k *= c2;
            h ^= k; h = rotl(h, 13); h = h * 5 + 0xe6546b64u;
        }

        const uint8_t* tail = bytes + blocks * 4;
        uint32_t k = 0;
        switch (size & 3) {
            case 3: k ^= static_cast<uint32_t>(tail[2]) << 16; [[fallthrough]];
            case 2: k ^= static_cast<uint32_t>(tail[1]) << 8; [[fallthrough]];
            case 1: k ^= tail[0]; k *= c1; k = rotl(k, 15); k *= c2; h ^= k;
        }

        h ^= static_cast<uint32_t>(size);
        return Mix32(h);
    }

    inline uint32_t Murmur3_32(std::string_view text, uint32_t seed = 0) { return Murmur3_32(text.data(), text.size(), seed); }
}

#pragma once

#include <cstdint>
#include <utility>

#include <glm/glm.hpp>

#include "engine/core/math/hash.hpp"

namespace Shard::Engine::Core {

    /// @brief SplitMix64 : turns one seed into a stream of well-distributed 64-bit values. Used to seed the other
    /// generators from a single number, and fine for anything that needs a few random values.
    class SplitMix64 {
    public:
        constexpr explicit SplitMix64(uint64_t seed = 0) : m_State(seed) {}
        constexpr uint64_t Next() {
            m_State += 0x9e3779b97f4a7c15ull;
            return Mix64(m_State);
        }
    private:
        uint64_t m_State;
    };

    /// @brief Seeded pseudo-random generator (PCG32, O'Neill 2014). Same seed -> same sequence on every platform and
    /// compiler, which is what replays, procedural generation and tests need ; std::mt19937 is deterministic too,
    /// but std::uniform_*_distribution is NOT (it differs between standard libraries), so use the methods below.
    ///
    /// 64 bits of state, 2^63 independent streams (the `stream` argument), passes statistical test suites, 2 words
    /// to copy around. NOT cryptographic.
    class Random {
    public:
        constexpr Random() : Random(0x853c49e6748fea9bull, 0xda3e39cb94b95bdbull) {}
        /// @param seed picks the sequence, @param stream picks one of 2^63 independent sequences for that seed
        constexpr explicit Random(uint64_t seed, uint64_t stream = 54) : m_State(0), m_Increment((stream << 1u) | 1u) {
            Next();
            m_State += seed;
            Next();
        }

        /// @brief Uniform 32 random bits.
        constexpr uint32_t Next() {
            const uint64_t old = m_State;
            m_State = old * 6364136223846793005ull + m_Increment;
            const uint32_t xorShifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
            const uint32_t rotation = static_cast<uint32_t>(old >> 59u);
            return (xorShifted >> rotation) | (xorShifted << ((~rotation + 1u) & 31u));
        }

        constexpr uint64_t NextU64() { return (uint64_t(Next()) << 32) | Next(); }

        /// @brief Uniform in [0, bound), without modulo bias. bound == 0 returns 0.
        constexpr uint32_t Below(uint32_t bound) {
            if (bound == 0) return 0;
            const uint32_t threshold = (0u - bound) % bound;       // reject the few values that would skew the result
            for (;;) {
                const uint32_t r = Next();
                if (r >= threshold) return r % bound;
            }
        }

        /// @brief Uniform integer in [lo, hi] (inclusive). lo > hi swaps them.
        constexpr int32_t Range(int32_t lo, int32_t hi) {
            if (lo > hi) { const int32_t t = lo; lo = hi; hi = t; }
            const uint32_t span = static_cast<uint32_t>(hi) - static_cast<uint32_t>(lo) + 1u;
            if (span == 0) return static_cast<int32_t>(Next());    // the whole 32-bit range
            return static_cast<int32_t>(static_cast<uint32_t>(lo) + Below(span));
        }

        /// @brief Uniform float in [0, 1) (24 random bits : every value is exactly representable).
        constexpr float NextFloat() { return static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f); }
        /// @brief Uniform double in [0, 1) (53 random bits).
        constexpr double NextDouble() { return static_cast<double>(NextU64() >> 11) * (1.0 / 9007199254740992.0); }

        constexpr float Range(float lo, float hi) { return lo + (hi - lo) * NextFloat(); }
        constexpr double Range(double lo, double hi) { return lo + (hi - lo) * NextDouble(); }

        /// @brief true with probability p.
        constexpr bool Chance(float p) { return NextFloat() < p; }

        /// @brief Uniform point inside the unit disk / sphere, on the unit circle / sphere, and a unit direction.
        glm::vec2 InUnitDisk();
        glm::vec3 InUnitSphere();
        glm::vec3 OnUnitSphere();
        glm::vec2 OnUnitCircle();

        /// @brief Fisher-Yates shuffle of [first, last).
        template <typename It>
        void Shuffle(It first, It last) {
            const auto count = static_cast<uint32_t>(last - first);
            for (uint32_t i = count; i > 1; --i) {
                const uint32_t j = Below(i);
                std::swap(*(first + (i - 1)), *(first + j));
            }
        }

        /// @brief A new generator on an independent stream, derived from this one : hand one to each job so that the
        /// result does not depend on which thread ran what.
        Random Split() {
            const uint64_t seed = NextU64();
            const uint64_t stream = NextU64();
            return Random(seed, stream);
        }

        constexpr bool operator==(const Random& o) const { return m_State == o.m_State && m_Increment == o.m_Increment; }

    private:
        uint64_t m_State;
        uint64_t m_Increment;
    };
}

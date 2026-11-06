#pragma once

#include <cstdint>
#include <utility>

#include <glm/glm.hpp>

#include "engine/core/math/hash.hpp"

namespace Shard::Engine::Core {

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

    // PCG32 (O'Neill 2014). Same sequence on every platform. std::uniform_*_distribution is not portable, use these methods. Not cryptographic.
    class Random {
    public:
        constexpr Random() : Random(0x853c49e6748fea9bull, 0xda3e39cb94b95bdbull) {}
        constexpr explicit Random(uint64_t seed, uint64_t stream = 54) : m_State(0), m_Increment((stream << 1u) | 1u) {
            Next();
            m_State += seed;
            Next();
        }

        constexpr uint32_t Next() {
            const uint64_t old = m_State;
            m_State = old * 6364136223846793005ull + m_Increment;
            const uint32_t xorShifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
            const uint32_t rotation = static_cast<uint32_t>(old >> 59u);
            return (xorShifted >> rotation) | (xorShifted << ((~rotation + 1u) & 31u));
        }

        constexpr uint64_t NextU64() {
            // Two statements: operand order of | is unspecified.
            const uint64_t hi = Next();
            const uint64_t lo = Next();
            return (hi << 32) | lo;
        }

        constexpr uint32_t Below(uint32_t bound) {
            if (bound == 0) return 0;
            const uint32_t threshold = (0u - bound) % bound;
            for (;;) {
                const uint32_t r = Next();
                if (r >= threshold) return r % bound;
            }
        }

        constexpr int32_t Range(int32_t lo, int32_t hi) {
            if (lo > hi) { const int32_t t = lo; lo = hi; hi = t; }
            const uint32_t span = static_cast<uint32_t>(hi) - static_cast<uint32_t>(lo) + 1u;
            if (span == 0) return static_cast<int32_t>(Next());
            return static_cast<int32_t>(static_cast<uint32_t>(lo) + Below(span));
        }

        constexpr float NextFloat() { return static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f); }
        constexpr double NextDouble() { return static_cast<double>(NextU64() >> 11) * (1.0 / 9007199254740992.0); }

        constexpr float Range(float lo, float hi) { return lo + (hi - lo) * NextFloat(); }
        constexpr double Range(double lo, double hi) { return lo + (hi - lo) * NextDouble(); }

        constexpr bool Chance(float p) { return NextFloat() < p; }

        glm::vec2 InUnitDisk();
        glm::vec3 InUnitSphere();
        glm::vec3 OnUnitSphere();
        glm::vec2 OnUnitCircle();

        template <typename It>
        void Shuffle(It first, It last) {
            const auto count = static_cast<uint32_t>(last - first);
            for (uint32_t i = count; i > 1; --i) {
                const uint32_t j = Below(i);
                std::swap(*(first + (i - 1)), *(first + j));
            }
        }

        Random Split() {
            const uint64_t seed = NextU64();
            const uint64_t stream = NextU64();
            return Random(seed, stream);
        }

        constexpr uint64_t State() const { return m_State; }
        constexpr uint64_t Increment() const { return m_Increment; }
        static constexpr Random FromState(uint64_t state, uint64_t increment) { Random r; r.m_State = state; r.m_Increment = increment; return r; }

        constexpr bool operator==(const Random& o) const { return m_State == o.m_State && m_Increment == o.m_Increment; }

    private:
        uint64_t m_State;
        uint64_t m_Increment;
    };
}

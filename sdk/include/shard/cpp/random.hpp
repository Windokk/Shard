// Shard C++ SDK : random numbers that a replay of the run reproduces (shard_plugin.h, random_stream).
//
//   class Kicker : public Shard::Script {
//       void OnPlay() override { m_Random = Shard::Random::Stream("Kicker/" + Owner().Name()); }
//       void OnTick(float) override { Owner().AddImpulse(m_Random.OnUnitSphere() * 4.f); }
//       Shard::Random m_Random;
//   };
//
// rand(), std::random_device and std::uniform_*_distribution give different numbers from one run, platform or standard library to the next, so
// a replay or a networked simulation that used them would drift. A stream starts from the run's seed, and this class (PCG32, the generator
// of the engine's own Core::Random) produces the same numbers everywhere. Not for cryptography.
#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

#include <shard/shard_plugin.h>
#include <shard/cpp/detail.hpp>
#include <shard/cpp/math.hpp>

namespace Shard {

    class Random {
    public:
        /// A generator with a fixed seed, for what does not depend on the run (the same sequence in every run)
        constexpr Random() : m_State(0x853c49e6748fea9bull), m_Increment(0xda3e39cb94b95bdbull) {}

        /// The stream `name` of this plugin for this run : the same name gives the same sequence when the run is replayed, and another name a
        /// sequence that does not depend on it. Name it after what draws from it ("Spawner/" + the actor's name), not after the order things
        /// started in. Throws Shard::Error if the host does not offer streams (an engine of ABI 1.4 or older).
        static Random Stream(std::string_view name)
        {
            const ShardPluginApi* api = detail::Interface<ShardPluginApi>(SHARD_IFACE_PLUGIN, 2);
            if (!api) throw Error(SHARD_E_UNSUPPORTED, "this host has no random streams (it needs shard.plugin 1.2)");
            ShardRandomState state{};
            detail::Check(api->random_stream(detail::PluginHandle(), Str(name), &state), "random_stream '" + std::string(name) + "'");
            Random r;
            r.m_State = state.state;
            r.m_Increment = state.increment;
            return r;
        }

        /// Uniform 32 random bits
        uint32_t Next()
        {
            const uint64_t old = m_State;
            m_State = old * 6364136223846793005ull + m_Increment;
            const uint32_t xorShifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
            const uint32_t rotation = static_cast<uint32_t>(old >> 59u);
            return (xorShifted >> rotation) | (xorShifted << ((~rotation + 1u) & 31u));
        }

        /// Uniform in [0, bound), without modulo bias. bound == 0 returns 0
        uint32_t Below(uint32_t bound)
        {
            if (bound == 0) return 0;
            const uint32_t threshold = (0u - bound) % bound;
            for (;;) {
                const uint32_t r = Next();
                if (r >= threshold) return r % bound;
            }
        }

        /// Uniform integer in [lo, hi], both included
        int32_t Range(int32_t lo, int32_t hi)
        {
            if (lo > hi) std::swap(lo, hi);
            const uint32_t span = static_cast<uint32_t>(hi) - static_cast<uint32_t>(lo) + 1u;
            if (span == 0) return static_cast<int32_t>(Next());
            return static_cast<int32_t>(static_cast<uint32_t>(lo) + Below(span));
        }

        /// Uniform float in [0, 1)
        float NextFloat() { return static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f); }
        float Range(float lo, float hi) { return lo + (hi - lo) * NextFloat(); }
        /// True with probability p
        bool Chance(float p) { return NextFloat() < p; }

        /// A uniform point inside the unit sphere, and a uniform unit direction
        Vec3 InUnitSphere()
        {
            for (;;) {
                const Vec3 p = Cube();
                if (Dot(p, p) <= 1.0f) return p;
            }
        }
        Vec3 OnUnitSphere()
        {
            for (;;) {
                const Vec3 p = Cube();
                const float lengthSquared = Dot(p, p);
                if (lengthSquared <= 1.0f && lengthSquared >= 1e-4f) return p / std::sqrt(lengthSquared);
            }
        }

    private:
        // Drawn in separate statements, z first : the arguments of a constructor have no defined evaluation order, and this is the order the
        // engine's own generator draws in, so both give the same vector from the same state
        Vec3 Cube()
        {
            const float z = Range(-1.0f, 1.0f);
            const float y = Range(-1.0f, 1.0f);
            const float x = Range(-1.0f, 1.0f);
            return { x, y, z };
        }

        uint64_t m_State;
        uint64_t m_Increment;
    };
}

#include "engine/core/determinism/determinism.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <random>

#include "engine/core/math/hash.hpp"

namespace Shard::Engine::Core::Determinism {

    namespace {
        std::mutex g_Mutex;
        bool g_Configured = false;
        bool g_Enabled = false;
        uint64_t g_Seed = 0;
        std::atomic<uint64_t> g_GuidCounter{0};

        uint64_t DrawSeedFromTheOS() {
            std::random_device device;
            uint64_t seed = (uint64_t(device()) << 32) | device();
            return HashCombine(seed, static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        }

        // The seed, drawn on first use when nobody configured one
        uint64_t SeedLocked() {
            if (!g_Configured) {
                g_Seed = DrawSeedFromTheOS();
                g_Configured = true;
            }
            return g_Seed;
        }
    }

    void Configure(uint64_t seed, bool deterministic) {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Seed = seed;
        g_Enabled = deterministic;
        g_Configured = true;
        g_GuidCounter.store(0, std::memory_order_relaxed);
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Configured = false;
        g_Enabled = false;
        g_Seed = 0;
        g_GuidCounter.store(0, std::memory_order_relaxed);
    }

    bool Enabled() {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_Enabled;
    }

    uint64_t Seed() {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return SeedLocked();
    }

    Random MakeStream(std::string_view name) {
        const uint64_t seed = Seed();
        // Two independent mixes of the name : the seed of the sequence, and which of the 2^63 streams it is
        const uint64_t nameHash = Fnv1a64(name);
        SplitMix64 mix(HashCombine(seed, nameHash));
        const uint64_t sequenceSeed = mix.Next();
        const uint64_t stream = mix.Next() >> 1;
        return Random(sequenceSeed, stream);
    }

    void NextGuidWords(uint64_t& high, uint64_t& low) {
        const uint64_t index = g_GuidCounter.fetch_add(1, std::memory_order_relaxed);
        SplitMix64 mix(HashCombine(Seed(), HashCombine(0x6775696468656c70ull, index)));
        high = mix.Next();
        low = mix.Next();
    }
}

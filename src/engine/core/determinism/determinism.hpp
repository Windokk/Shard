#pragma once

#include <cstdint>
#include <string_view>

#include "engine/core/math/random.hpp"

namespace Shard::Engine::Core::Determinism {

    // Enabled means: one fixed step per frame, single-threaded physics, counted GUIDs, no hot reload. Call Configure once, before anything draws a random number.
    void Configure(uint64_t seed, bool deterministic);

    void Reset();

    bool Enabled();
    uint64_t Seed();

    Random MakeStream(std::string_view name);

    void NextGuidWords(uint64_t& high, uint64_t& low);
}

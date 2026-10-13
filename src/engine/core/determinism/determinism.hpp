#pragma once

#include <cstdint>
#include <string_view>

#include "engine/core/math/random.hpp"

namespace Shard::Engine::Core::Determinism {

    /// @brief The seed every random stream of the engine derives from, and whether the run is a deterministic one.
    ///
    /// A run always has a seed : the one it was given (--seed, a replay file), or one drawn from the OS when it was not,
    /// which the engine logs, so that any run can be reproduced afterwards. `Enabled` is what asks for the rest of what
    /// reproducibility needs : every frame lasts exactly one fixed step whatever the clock says, physics runs on one thread
    /// (the order of the contact callbacks is then the same), GUIDs are counted instead of drawn, nothing reads the disk
    /// behind the simulation's back (hot reload).
    ///
    /// Call Configure once, before the engine creates anything that draws a random number.
    void Configure(uint64_t seed, bool deterministic);

    /// @brief Drops the seed and the mode : a fresh seed is drawn the next time one is asked for (tests).
    void Reset();

    bool Enabled();
    uint64_t Seed();

    /// @brief A generator of its own for a system, derived from the seed and the system's name : the same name gives the
    /// same sequence in every run with that seed, whatever order the systems start in and however many threads they run on.
    /// Keep the generator in the system (call this once, not every frame).
    ///
    ///     Core::Random m_Random = Core::Determinism::MakeStream("physics.spawner");
    Random MakeStream(std::string_view name);

    /// @brief The next GUID of the deterministic sequence (see Guid::Generate) : derived from the seed and a counter.
    /// Only meaningful while Enabled().
    void NextGuidWords(uint64_t& high, uint64_t& low);
}

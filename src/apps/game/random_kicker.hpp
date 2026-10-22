#pragma once

#include <shard/game.hpp>

/// @brief Kicks the physics body of its actor in a random direction every `interval` frames. The reference for gameplay that needs random
/// numbers : it takes its generator from a stream of the run (shard::Random::Stream, derived from the run's seed) instead of rand() or
/// std::random_device, so a replay of the run kicks the same way. The determinism tests use it for that.
class RandomKicker : public shard::Script {
public:
    void OnPlay() override;
    void OnTick(float deltaTime) override;

    SHARD_FIELD(int, interval, Tooltip("Frames between two kicks")) = 30;
    SHARD_FIELD(float, strength, Tooltip("Impulse of a kick (mass * units per second)")) = 4.0f;

    SHARD_FIELD(int, frames, Runtime()) = 0;
    SHARD_FIELD(int, kicks, Runtime()) = 0;

private:
    shard::Random m_Random;
};

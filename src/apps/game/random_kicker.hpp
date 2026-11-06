#pragma once

#include <shard/game.hpp>

// Takes its generator from a run stream (Shard::Random::Stream, derived from the run's seed) instead of rand() or std::random_device, so a replay kicks the same way. Used by the determinism tests.
class RandomKicker : public Shard::Script {
public:
    void OnPlay() override;
    void OnTick(float deltaTime) override;

    SHARD_FIELD(int, interval, Tooltip("Frames between two kicks")) = 30;
    SHARD_FIELD(float, strength, Tooltip("Impulse of a kick (mass * units per second)")) = 4.0f;

    SHARD_FIELD(int, frames, Runtime()) = 0;
    SHARD_FIELD(int, kicks, Runtime()) = 0;

private:
    Shard::Random m_Random;
};

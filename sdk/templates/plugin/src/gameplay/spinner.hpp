#pragma once

#include "../module.hpp"

class Spinner : public Shard::Script {
public:
    SHARD_FIELD(float, degreesPerSecond, Range(-720.f, 720.f), Tooltip("Turn speed")) = 90.f;
    SHARD_FIELD(Shard::Vec3, axis) = { 0.f, 1.f, 0.f };

    void OnTick(float deltaTime) override;
};

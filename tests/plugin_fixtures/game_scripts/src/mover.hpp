#pragma once

#include <shard/game.hpp>

class Mover : public Shard::Script {
public:
    float speed = 2.f;
    Shard::Vec3 direction{ 1.f, 0.f, 0.f };

    void OnTick(float deltaTime) override;
};

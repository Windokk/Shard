#pragma once

#include <shard/game.hpp>

class Turret : public Shard::Script {
public:
    Shard::Actor target;
    std::string note = "idle";
    float power = 1.f;

    void OnTick(float deltaTime) override;
};

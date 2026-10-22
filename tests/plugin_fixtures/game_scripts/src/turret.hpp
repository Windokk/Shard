#pragma once

#include <shard/game.hpp>

// A script with a reference to another actor and a std::string member, both fields
class Turret : public shard::Script {
public:
    shard::Actor target;                // another actor of the world, saved by its path
    std::string note = "idle";          // the plugin's own string : the host reads it through callbacks
    float power = 1.f;

    void OnTick(float deltaTime) override;
};

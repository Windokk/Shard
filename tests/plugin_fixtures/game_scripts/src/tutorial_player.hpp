#pragma once

#include <shard/game.hpp>

class Bullet : public Shard::Script {
public:
    float life = 3.f;
    void OnTick(float deltaTime) override;
};

class Player : public Shard::Script {
public:
    float speed = 5.f;
    void OnTick(float dt) override;
};

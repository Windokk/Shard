// The scripts of the tutorial (docs/docs/tutorials.md, "Talk to the world"), built with the fixture so that what the page shows compiles
#pragma once

#include <shard/game.hpp>

class Bullet : public shard::Script {
public:
    float life = 3.f;
    void OnTick(float deltaTime) override;
};

class Player : public shard::Script {
public:
    float speed = 5.f;
    void OnTick(float dt) override;
};

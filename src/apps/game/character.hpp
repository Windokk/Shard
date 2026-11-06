#pragma once

#include <shard/game.hpp>

class Character : public Shard::Script {
public:
    void OnPlay() override;
    void OnTick(float deltaTime) override;

    SHARD_FIELD(float, speed, Tooltip("Units per second")) = 30.0f;
    SHARD_FIELD(float, mouseSensitivity) = 0.1f;

    SHARD_FIELD(double, lockedMouseX, Runtime()) = 0;
    SHARD_FIELD(double, lockedMouseY, Runtime()) = 0;
    SHARD_FIELD(bool, firstFrame, Runtime()) = true;
    SHARD_FIELD(float, pitch, Runtime()) = 0.0f;
    SHARD_FIELD(float, yaw, Runtime()) = 0.0f;
};

#pragma once

#include <shard/game.hpp>

/// @brief A free camera : W A S D move it, and the mouse turns it while the left button is held.
class Character : public shard::Script {
public:
    void OnPlay() override;
    void OnTick(float deltaTime) override;

    SHARD_FIELD(float, speed, Tooltip("Units per second")) = 30.0f;
    SHARD_FIELD(float, mouseSensitivity) = 0.1f;

    // What the look needs from one frame to the next : shown in the inspector, not saved
    SHARD_FIELD(double, lockedMouseX, Runtime()) = 0;
    SHARD_FIELD(double, lockedMouseY, Runtime()) = 0;
    SHARD_FIELD(bool, firstClick, Runtime()) = true;
    SHARD_FIELD(float, pitch, Runtime()) = 0.0f;
    SHARD_FIELD(float, yaw, Runtime()) = 0.0f;
};

#pragma once

#include <vector>

#include <shard/game.hpp>

/// @brief Fires a physical bullet where the actor looks when the left mouse button is pressed
class Gun : public Shard::Script {
public:
    void OnTick(float deltaTime) override;

    SHARD_FIELD(float, gunShotIntensity, Range(0.f, 1000.f)) = 30.f;
    SHARD_FIELD(float, bulletLifetime, Range(0.f, 60.f), Tooltip("Seconds before a bullet is destroyed")) = 3.f;
    SHARD_FIELD(Shard::AssetId, mesh, Tooltip("The mesh to shoot"));
    SHARD_FIELD(Shard::AssetId, material, Tooltip("The material of the mesh to shoot"));
    SHARD_FIELD(Shard::AssetId, sound, Tooltip("The sound to play on shoot"));
    SHARD_FIELD(float, capsuleYOffset) = 0.3f;

private:
    /// @brief Fires a bullet that is destroyed `time` seconds later
    void Shoot(float time);

    struct SBullet {
        Shard::Actor actor;
        float remaining;
    };
    std::vector<SBullet> bullets;
};

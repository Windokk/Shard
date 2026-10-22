#pragma once

#include "../module.hpp"

// A script: a class that lives on an actor, like the engine's own scripts. Add the component "<plugin id>.Spinner" to an actor in the editor
// and it turns in play mode. Its fields show in the inspector and are saved with the world : SHARD_FIELD declares one where the member is, with
// its options (Range, Tooltip, Runtime...), and the default value goes after it.
class Spinner : public Shard::Script {
public:
    SHARD_FIELD(float, degreesPerSecond, Range(-720.f, 720.f), Tooltip("Turn speed")) = 90.f;
    SHARD_FIELD(Shard::Vec3, axis) = { 0.f, 1.f, 0.f };

    void OnTick(float deltaTime) override;
};

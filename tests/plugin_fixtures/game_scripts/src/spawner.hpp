#pragma once

#include <shard/game.hpp>

class Spawner : public Shard::Script {
public:
    int32_t count = 2;

    void OnPlay() override;
};

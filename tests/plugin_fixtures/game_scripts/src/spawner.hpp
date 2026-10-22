#pragma once

#include <shard/game.hpp>

class Spawner : public shard::Script {
public:
    int32_t count = 2;

    void OnPlay() override;
};

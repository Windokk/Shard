#pragma once

#include <vector>

#include <shard/game.hpp>

class Bullet : public shard::Script {
public:

    void OnContactAdded(const shard::Contact&) override;

private:

};

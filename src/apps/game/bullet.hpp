#pragma once

#include <vector>

#include <shard/game.hpp>

class Bullet : public Shard::Script {
public:

    void OnContactAdded(const Shard::Contact&) override;

private:

};

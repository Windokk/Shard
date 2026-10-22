// A script whose fields are declared where the members are (SHARD_FIELD), each with its own options
#pragma once

#include <string>

#include <shard/game.hpp>

class Declared : public Shard::Script {
public:
    SHARD_FIELD(float, power, Range(0.f, 10.f), Tooltip("how hard it hits")) = 3.f;
    SHARD_FIELD(int, ammo, Runtime()) = 12;                         // shown by the inspector, not saved
    SHARD_FIELD(Shard::Vec3, aim) = { 0.f, 0.f, -1.f };
    SHARD_FIELD(std::string, label) = "declared";
    SHARD_FIELD(float, secret, Hidden()) = 9.f;                     // neither shown nor saved
    float plain = 5.f;                                              // not a field at all
};

#include "turret.hpp"

#include "stats.hpp"

void Turret::OnTick(float)
{
    GetStats().lastNote = note;
    if (!target) return;
    Owner().LookAt(target.WorldPosition());
    GetStats().targetDistance = shard::Distance(Owner().WorldPosition(), target.WorldPosition());
    ++GetStats().aimed;
}

SHARD_REGISTER_SCRIPT(Turret, (target)(note)(power))

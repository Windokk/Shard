#include "random_kicker.hpp"

void RandomKicker::OnPlay()
{
    // A stream of its own : named after the actor, so it does not depend on which script started first
    m_Random = shard::Random::Stream("RandomKicker/" + Owner().Name());
    frames = 0;
    kicks = 0;
}

void RandomKicker::OnTick(float)
{
    ++frames;
    if (interval <= 0 || frames % interval != 0)
        return;

    if (Owner().AddImpulse(m_Random.OnUnitSphere() * strength))     // false : the actor has no body
        ++kicks;
}

SHARD_REGISTER_SCRIPT(RandomKicker)

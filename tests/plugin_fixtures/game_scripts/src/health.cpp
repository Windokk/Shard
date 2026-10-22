#include "health.hpp"

#include "stats.hpp"

void Health::OnCreate()
{
    ++GetStats().created;
}

void Health::OnPlay()
{
    ++GetStats().played;
    current = maxHealth;
    if (label.Get().empty()) label = "unnamed";
}

void Health::OnTick(float deltaTime)
{
    ++GetStats().ticks;
    GetStats().lastDt = deltaTime;
    // Dead : the actor goes, and this script with it, in the middle of the world's tick
    if (current <= 0.f) Owner().Destroy();
}

void Health::OnFixedTick(float fixedDeltaTime)
{
    ++GetStats().fixedTicks;
    GetStats().lastFixedDt = fixedDeltaTime;
}

void Health::OnLateTick(float)
{
    ++GetStats().lateTicks;
}

void Health::OnStop()
{
    ++GetStats().stopped;
}

void Health::OnDestroyed()
{
    ++GetStats().destroyed;
}

void Health::Damage(float amount)
{
    current -= amount;
    ++timesHurt;
}

SHARD_REGISTER_SCRIPT_EX(Health, (maxHealth)(current)(timesHurt)(label)(icon),
    ({ "maxHealth", 0, 1.f, 1000.f, "hit points at the start" }, { "timesHurt", SHARD_FIELD_RUNTIME, 0.f, 0.f, "" }))

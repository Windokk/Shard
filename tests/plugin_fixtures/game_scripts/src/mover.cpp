#include "mover.hpp"

#include "health.hpp"
#include "stats.hpp"

void Mover::OnTick(float deltaTime)
{
    // The keyboard : A and D steer along the direction, Space hurts the Health script of the same actor (a script of another file)
    const float steer = shard::Input::Axis(shard::Key::A, shard::Key::D);
    Owner().Translate(direction * (speed * deltaTime * (1.f + steer)));
    if (shard::Input::WasPressed(shard::Key::Space)) {
        if (Health* health = Owner().GetScript<Health>()) {
            health->Damage(60.f);
            ++GetStats().crossScript;
        }
    }
}

SHARD_REGISTER_SCRIPT(Mover, (speed)(direction))

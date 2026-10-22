#include "bullet.hpp"

void Bullet::OnContactAdded(const Shard::Contact &)
{
    Owner().Destroy();
}

SHARD_REGISTER_SCRIPT(Bullet)


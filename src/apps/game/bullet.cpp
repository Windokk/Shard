#include "bullet.hpp"

void Bullet::OnContactAdded(const shard::Contact &)
{
    Owner().Destroy();
}

SHARD_REGISTER_SCRIPT(Bullet)


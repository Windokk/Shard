// src/ecs/projectiles.cpp of the tutorial ("Systems, for what there are thousands of")
#include <shard/game.hpp>

struct Projectile { shard::Vec3 velocity; float life = 3.f; };
SHARD_DEFINE_COMPONENT(Projectile, (velocity)(life))        // found anywhere with shard::ComponentOf<Projectile>()

SHARD_ON_LOAD(ProjectileSystem, ctx)                  // runs in OnLoad, after every component is registered
{
    const auto projectile = shard::ComponentOf<Projectile>();
    const auto pose = ctx.Components().Find<shard::Pose>("shard.LocalTransform");
    ctx.Systems().Add("fly", shard::Phase::Update).Writes(projectile, pose).Run([projectile, pose](shard::SystemCtx& s) {
        s.Each(projectile, [&](shard::Entity e, Projectile& b) {
            b.life -= s.DeltaTime();
            if (shard::Pose* p = s.Get(e, pose)) { shard::Pose moved = *p; moved.position += b.velocity * s.DeltaTime(); s.SetLocalTransform(e, moved); }
            if (b.life <= 0.f) s.Destroy(e);
        });
    });
}

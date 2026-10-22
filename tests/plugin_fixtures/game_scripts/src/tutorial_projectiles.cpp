// src/ecs/projectiles.cpp of the tutorial ("Systems, for what there are thousands of")
#include <shard/game.hpp>

struct Projectile { Shard::Vec3 velocity; float life = 3.f; };
SHARD_DEFINE_COMPONENT(Projectile, (velocity)(life))        // found anywhere with Shard::ComponentOf<Projectile>()

SHARD_ON_LOAD(ProjectileSystem, ctx)                  // runs in OnLoad, after every component is registered
{
    const auto projectile = Shard::ComponentOf<Projectile>();
    const auto pose = ctx.Components().Find<Shard::Pose>("shard.LocalTransform");
    ctx.Systems().Add("fly", Shard::Phase::Update).Writes(projectile, pose).Run([projectile, pose](Shard::SystemCtx& s) {
        s.Each(projectile, [&](Shard::Entity e, Projectile& b) {
            b.life -= s.DeltaTime();
            if (Shard::Pose* p = s.Get(e, pose)) { Shard::Pose moved = *p; moved.position += b.velocity * s.DeltaTime(); s.SetLocalTransform(e, moved); }
            if (b.life <= 0.f) s.Destroy(e);
        });
    });
}

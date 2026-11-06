#include <shard/game.hpp>

#include "../module.hpp"

struct Drift {
    Shard::Vec3 velocity{ 0.f, 0.f, 0.f };
    float age = 0.f;
};
SHARD_DEFINE_COMPONENT(Drift, (velocity)(age))

SHARD_ON_LOAD(DriftSystem, ctx)
{
    const auto drift = Shard::ComponentOf<Drift>();
    const auto pose = ctx.Components().Find<Shard::Pose>("shard.LocalTransform");
    ctx.Systems().Add("drift", Shard::Phase::Update)
        .Writes(drift, pose)
        .Run([drift, pose](Shard::SystemCtx& s) {
            // Read once before the loop. Rare work (events, logging) goes in a branch the loop seldom takes, the SDK keeps those calls out of line.
            const float dt = s.DeltaTime() * SpinScale().Get();
            s.Each(drift, [&](Shard::Entity entity, Drift& d) {
                d.age += dt;
                if (Shard::Pose* p = s.Get(entity, pose)) {
                    Shard::Pose moved = *p;
                    moved.position += d.velocity * dt;
                    s.SetLocalTransform(entity, moved);
                }
            });
        });
}

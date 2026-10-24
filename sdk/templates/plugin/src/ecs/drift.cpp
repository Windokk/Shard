// The other way to write gameplay: data in components and systems that work on all of them at once, in parallel when they do not conflict. It
// fits what there are thousands of (bullets, particles, crowd members). It lives in its own file and registers itself like a script does.
#include <shard/game.hpp>

#include "../module.hpp"

struct Drift {
    Shard::Vec3 velocity{ 0.f, 0.f, 0.f };      // units per second
    float age = 0.f;
};
SHARD_DEFINE_COMPONENT(Drift, (velocity)(age))

SHARD_ON_LOAD(DriftSystem, ctx)
{
    const auto drift = Shard::ComponentOf<Drift>();
    // The engine's own component of the pose: an actor is read through it and moved with SystemCtx::SetLocalTransform, which asks for it in writes
    const auto pose = ctx.Components().Find<Shard::Pose>("shard.LocalTransform");
    ctx.Systems().Add("drift", Shard::Phase::Update)
        .Writes(drift, pose)
        .Run([drift, pose](Shard::SystemCtx& s) {
            // Read once, before the loop. Anything rare (an event with s.Emit, a log line) belongs in a branch the loop seldom takes : the SDK keeps
            // those calls out of line, so the loop stays as tight as a system of the engine's
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

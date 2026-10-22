// A component of the ECS kind and a system that works on it, defined away from the scripts
#include <shard/game.hpp>

#include "stats.hpp"

struct Velocity {
    float x = 0.f, y = 0.f, z = 0.f;
};
SHARD_DEFINE_COMPONENT(Velocity, (x)(y)(z))

SHARD_ON_LOAD(Drift, ctx)
{
    // Components are registered before this runs, whichever file defined them
    const auto velocity = shard::ComponentOf<Velocity>();
    ctx.Systems().Add("drift", shard::Phase::Update).Writes(velocity).Run([velocity](shard::SystemCtx& s) {
        s.Each(velocity, [&](Velocity& v) {
            v.x += 1.f;
            ++GetStats().driftSteps;
        });
    });
}

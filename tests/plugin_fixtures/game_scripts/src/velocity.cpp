#include <shard/game.hpp>

#include "stats.hpp"

struct Velocity {
    float x = 0.f, y = 0.f, z = 0.f;
};
SHARD_DEFINE_COMPONENT(Velocity, (x)(y)(z))

SHARD_ON_LOAD(Drift, ctx)
{
    const auto velocity = Shard::ComponentOf<Velocity>();
    ctx.Systems().Add("drift", Shard::Phase::Update).Writes(velocity).Run([velocity](Shard::SystemCtx& s) {
        s.Each(velocity, [&](Velocity& v) {
            v.x += 1.f;
            ++GetStats().driftSteps;
        });
    });
}

#include "spawner.hpp"

#include "health.hpp"
#include "stats.hpp"

void Spawner::OnPlay()
{
    shard::Scene scene = GetScene();
    for (int i = 0; i < count; ++i) {
        shard::Actor minion = scene.Create("Minion" + std::to_string(i), Owner());
        minion.AddScript<Health>();
        minion.SetPosition({ float(i), 0.f, 0.f });
        ++GetStats().minions;
    }
    GetStats().undeclaredTag = Owner().AddTag("NotDeclaredAnywhere") ? 1 : 0;
    const shard::Actor hero = scene.Find("Hero");
    GetStats().foundName = hero ? hero.Name() : "<none>";
    const shard::Actor spawned = scene.Spawn(R"({ "name": "FromJson", "components": [] })");
    if (spawned && spawned.Name() == "FromJson") ++GetStats().spawnedFromJson;
}

SHARD_REGISTER_SCRIPT(Spawner, (count))

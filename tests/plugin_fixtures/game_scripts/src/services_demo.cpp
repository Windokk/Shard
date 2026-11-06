#include <shard/game.hpp>

#include "stats.hpp"

class Misc : public Shard::Script {
public:
    void OnPlay() override
    {
        GetStats().worldLoad = Shard::Worlds::Load("worlds/none.world") ? 1 : 0;
        GetStats().playAudio = Owner().PlayAudio() ? 1 : 0;
        try {
            Shard::Audio::PlayOneShot("sounds/none.wav", Owner().WorldPosition());
            GetStats().oneShot = 1;
        } catch (const Shard::Error& e) {
            GetStats().oneShot = e.Result();
        }
        Shard::Random a = Shard::Random::Stream("dice"), b = Shard::Random::Stream("dice"), c = Shard::Random::Stream("other");
        const uint32_t first = a.Next();
        GetStats().randomFirst = int(first);
        GetStats().randomSame = first == b.Next() ? 1 : 0;
        GetStats().randomOther = first != c.Next() ? 1 : 0;
        const Shard::Vec3 sphere = Shard::Random::Stream("sphere").OnUnitSphere();
        GetStats().sphereX = sphere.x; GetStats().sphereY = sphere.y; GetStats().sphereZ = sphere.z;

        const Shard::Actor crate = GetScene().SpawnPrefab("crate");
        if (crate && crate.Name() == "Crate" && crate.Children().size() == 1) ++GetStats().prefab;
        try {
            GetScene().SpawnPrefab("no_such_prefab");
        } catch (const Shard::Error&) {
            ++GetStats().missingPrefab;
        }
    }
};
SHARD_REGISTER_SCRIPT_BARE(Misc)

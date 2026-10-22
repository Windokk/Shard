// Worlds, prefabs and sounds from a script
#include <shard/game.hpp>

#include "stats.hpp"

class Misc : public shard::Script {
public:
    void OnPlay() override
    {
        GetStats().worldLoad = shard::Worlds::Load("worlds/none.world") ? 1 : 0;       // the project has no such world
        GetStats().playAudio = Owner().PlayAudio() ? 1 : 0;                              // the actor has no audio component
        try {
            shard::Audio::PlayOneShot("sounds/none.wav", Owner().WorldPosition());
            GetStats().oneShot = 1;
        } catch (const shard::Error& e) {
            GetStats().oneShot = e.Result();                                             // the project has no such clip
        }
        // Streams : the same name gives the same numbers, another name does not ; the engine's own generator gives the same ones
        shard::Random a = shard::Random::Stream("dice"), b = shard::Random::Stream("dice"), c = shard::Random::Stream("other");
        const uint32_t first = a.Next();
        GetStats().randomFirst = int(first);
        GetStats().randomSame = first == b.Next() ? 1 : 0;
        GetStats().randomOther = first != c.Next() ? 1 : 0;
        const shard::Vec3 sphere = shard::Random::Stream("sphere").OnUnitSphere();
        GetStats().sphereX = sphere.x; GetStats().sphereY = sphere.y; GetStats().sphereZ = sphere.z;

        const shard::Actor crate = GetScene().SpawnPrefab("crate");
        if (crate && crate.Name() == "Crate" && crate.Children().size() == 1) ++GetStats().prefab;
        try {
            GetScene().SpawnPrefab("no_such_prefab");
        } catch (const shard::Error&) {
            ++GetStats().missingPrefab;
        }
    }
};
SHARD_REGISTER_SCRIPT_BARE(Misc)

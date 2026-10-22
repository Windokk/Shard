// What a script does with the physics : forces, contacts and a ray
#include <shard/game.hpp>

#include "stats.hpp"

class Ball : public shard::Script {
public:
    void OnFixedTick(float) override
    {
        if (Owner().AddForce({ 0.f, 0.f, 0.f })) ++GetStats().forced;      // true : the actor has a live body
    }
    void OnContactAdded(const shard::Contact& contact) override
    {
        ++GetStats().contactsAdded;
        GetStats().contactNormalY = contact.normal.y;
        GetStats().contactPenetration = contact.penetration;
        GetStats().contactOther = contact.other.Name();
    }
    void OnContactEnded(const shard::Contact&) override { ++GetStats().contactsEnded; }
};
SHARD_REGISTER_SCRIPT_BARE(Ball)

class Probe : public shard::Script {
public:
    void OnPlay() override
    {
        // Straight down from the actor, not counting its own body
        if (auto hit = shard::Physics::Raycast(Owner().WorldPosition(), { 0.f, -1.f, 0.f }, 50.f, shard::Layers::All, Owner())) {
            ++GetStats().rayHits;
            GetStats().rayDistance = hit->distance;
            GetStats().rayNormalY = hit->normal.y;
            GetStats().rayActor = hit->actor.Name();
        }
    }
};
SHARD_REGISTER_SCRIPT_BARE(Probe)

#include <shard/game.hpp>

#include "stats.hpp"

class Zone : public Shard::Script {
public:
    static constexpr uint32_t kCollisionEvents = Shard::CollisionKinds::OverlapBegin | Shard::CollisionKinds::OverlapEnd;

    void OnOverlapBegin(const Shard::CollisionEvent& e) override
    {
        ++GetStats().overlapBegins;
        GetStats().overlapOther = e.other.Name();
        GetStats().overlapCount = int(Owner().Overlapping().size());
    }
    void OnOverlapEnd(const Shard::CollisionEvent& e) override
    {
        ++GetStats().overlapEnds;
        GetStats().overlapCause = int(e.cause);
    }
    void OnHit(const Shard::CollisionEvent&) override { ++GetStats().zoneHits; }
};
SHARD_REGISTER_SCRIPT_BARE(Zone)

class Faller : public Shard::Script {
public:
    static constexpr uint32_t kCollisionEvents = Shard::CollisionKinds::Hit;

    void OnHit(const Shard::CollisionEvent& e) override
    {
        ++GetStats().hits;
        GetStats().hitSpeed = e.impactSpeed;
        GetStats().hitNormalY = e.normal.y;
    }
    void OnOverlapBegin(const Shard::CollisionEvent&) override { ++GetStats().fallerOverlaps; }
};
SHARD_REGISTER_SCRIPT_BARE(Faller)

class Quiet : public Shard::Script {
public:
    void OnOverlapBegin(const Shard::CollisionEvent&) override { ++GetStats().quietEvents; }
    void OnHit(const Shard::CollisionEvent&) override { ++GetStats().quietEvents; }
};
SHARD_REGISTER_SCRIPT_BARE(Quiet)

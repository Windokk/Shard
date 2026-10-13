#include "random_kicker.hpp"

#include "engine/physics/physics_body.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"

#include "engine/world/components/script.reflection.hpp"

using namespace Shard::Engine;

void RandomKicker::OnPlay() {
    // A stream of its own : named after the actor, so it does not depend on which script started first
    m_Random = Core::GetEngine().MakeRandomStream("RandomKicker/" + parent->GetName() + "/" + std::to_string(GetLocalId()));
    frames = 0;
    kicks = 0;
}

void RandomKicker::OnTick() {
    ++frames;
    if (interval <= 0 || frames % interval != 0)
        return;

    auto body = parent->GetComponent<Objects::Components::PhysicsBody>();
    if (!body)
        return;

    body->AddImpulse(m_Random.OnUnitSphere() * strength);
    ++kicks;
}

REGISTER_COMPONENT(RandomKicker);

#pragma once

#include "engine/physics/physics_manager.hpp"

#include "engine/world/event_system.hpp"

namespace Shard::Engine::Objects::Components{
    class PhysicsBody;
}

namespace Shard::Engine::Events {

    // Sent once the physics step is over, on the thread that stepped it (see PhysicsManager::StepSimulation) : the scripts may read
    // and change bodies, or destroy actors. contactSettings is a copy : the contact was already solved, changing it has no effect.

    struct ContactAddedEvent : public Event {
        const Objects::Components::PhysicsBody& otherBody;
        const ContactManifold &contactManifold;
        ContactSettings &contactSettings;
        ContactAddedEvent(const Objects::Components::PhysicsBody& b2, const ContactManifold &manifold, ContactSettings &settings, Core::ObjectID source)
             : Event(source), otherBody(b2), contactManifold(manifold), contactSettings(settings) {}
    };

    struct ContactPersistedEvent : public Event {
        const Objects::Components::PhysicsBody& otherBody;
        const ContactManifold &contactManifold;
        ContactSettings &contactSettings;
        ContactPersistedEvent(const Objects::Components::PhysicsBody& b2, const ContactManifold &manifold, ContactSettings &settings, Core::ObjectID source)
             : Event(source), otherBody(b2), contactManifold(manifold), contactSettings(settings) {}
    };

    struct ContactRemovedEvent : public Event {
        const Objects::Components::PhysicsBody& otherBody;
        ContactRemovedEvent(
        const Objects::Components::PhysicsBody& b2, Core::ObjectID source) : Event(source), otherBody(b2) {}
    };
}

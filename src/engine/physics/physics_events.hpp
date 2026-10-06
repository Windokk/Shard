#pragma once

#include "engine/physics/physics_manager.hpp"

#include "engine/world/event_system.hpp"

namespace Shard::Engine::Objects::Components{
    class PhysicsBody;
}

namespace Shard::Engine::Events {

    struct ContactAddedEvent : public Event {
        const Objects::Components::PhysicsBody& otherBody;
        const ContactManifold &contactManifold;
        ContactSettings &contactSettings;
        ContactAddedEvent(const Objects::Components::PhysicsBody& b2, const ContactManifold &manifold, ContactSettings &settings, Core::ObjectID source)
             : otherBody(b2), contactManifold(manifold), contactSettings(settings), Event(source) {}
    };

    struct ContactPersistedEvent : public Event {
        const Objects::Components::PhysicsBody& otherBody;
        const ContactManifold &contactManifold;
        ContactSettings &contactSettings;
        ContactPersistedEvent(const Objects::Components::PhysicsBody& b2, const ContactManifold &manifold, ContactSettings &settings, Core::ObjectID source)
             : otherBody(b2), contactManifold(manifold), contactSettings(settings), Event(source) {}
    };

    struct ContactRemovedEvent : public Event {
        const Objects::Components::PhysicsBody& otherBody;
        ContactRemovedEvent(
        const Objects::Components::PhysicsBody& b2, Core::ObjectID source) : otherBody(b2), Event(source) {}
    };
}

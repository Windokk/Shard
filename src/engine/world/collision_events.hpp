#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "engine/core/collision/collision_types.hpp"
#include "engine/core/tags/tag.hpp"
#include "engine/core/tags/tag_query.hpp"
#include "engine/world/event_system.hpp"

namespace Shard::Engine::Objects {
    class Actor;
}

namespace Shard::Engine::Events {

    struct CollisionEvent : public Event {
        Collision::EventKind kind = Collision::EventKind::Hit;
        uint64_t step = 0;
        std::shared_ptr<Objects::Actor> self;
        std::shared_ptr<Objects::Actor> other;
        uint64_t selfCollider = 0;
        uint64_t otherCollider = 0;
        glm::dvec3 point = glm::dvec3(0.0);
        glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
        float impactSpeed = 0.0f;
        float impulse = 0.0f;
        Core::Tag surface;
        Collision::EndCause cause = Collision::EndCause::None;

        CollisionEvent() : Event(Core::ObjectID(0)) {}
    };

    using CollisionHandler = std::function<void(const CollisionEvent&)>;

    struct SubscribeOptions {
        Core::TagQuery other;
        float minImpactSpeed = 0.0f;
        bool once = false;
        std::string label;
    };

    class ICollisionHost {
        public:
            virtual ~ICollisionHost() = default;
            virtual uint64_t Subscribe(const std::shared_ptr<Objects::Actor>& actor, uint32_t kinds, SubscribeOptions options, CollisionHandler handler, uint64_t ownerToken = 0) = 0;
            virtual size_t UnsubscribeOwner(uint64_t ownerToken) = 0;
    };
}

namespace Shard::Engine::Physics {
    constexpr uint32_t KindBit(Collision::EventKind kind) { return 1u << uint32_t(kind); }
    constexpr uint32_t kAllCollisionKinds = 0x7u;
    using Events::SubscribeOptions;
}

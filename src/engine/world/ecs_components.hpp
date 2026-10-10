#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Shard::Engine::Worlds {

    /// @brief On the entity of every actor : the ObjectID of the actor it stands for.
    ///
    /// Actor / Component stay the API the editor, the scripts and the serialization use (the facade) ; each actor owns an entity of
    /// the engine's Ecs::Registry that mirrors it (its parent / children, and the data systems work on). A system that finds an
    /// entity through a query gets back to the actor with this link ; the actor reaches its entity with Actor::GetEntity().
    struct ActorLink {
        int objectId = -1;
    };

    /// @brief The local transform of an actor, kept up to date on its entity by Transform (every time a local field changes).
    /// This is what systems read : contiguous, no pointer chasing through the actor and its components.
    struct LocalTransform {
        glm::vec3 position = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
    };

    /// @brief Present on the entity of an actor that is deactivated. Queries that must skip such actors say
    /// EachExcluding<Exclude<Disabled>, ...>.
    struct Disabled {
        uint8_t unused = 0;
    };
}

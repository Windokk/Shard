#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Shard::Engine::Worlds {

    struct ActorLink {
        int objectId = -1;
    };

    struct LocalTransform {
        glm::vec3 position = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
    };

    struct Disabled {
        uint8_t unused = 0;
    };
}

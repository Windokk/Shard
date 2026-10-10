#include "engine/core/math/random.hpp"

#include <cmath>

namespace Shard::Engine::Core {

    glm::vec2 Random::InUnitDisk() {
        // Rejection sampling : uniform, and the loop runs 1.27 times on average
        for (;;) {
            const glm::vec2 p(Range(-1.0f, 1.0f), Range(-1.0f, 1.0f));
            if (glm::dot(p, p) <= 1.0f) return p;
        }
    }

    glm::vec3 Random::InUnitSphere() {
        for (;;) {
            const glm::vec3 p(Range(-1.0f, 1.0f), Range(-1.0f, 1.0f), Range(-1.0f, 1.0f));
            if (glm::dot(p, p) <= 1.0f) return p;
        }
    }

    glm::vec3 Random::OnUnitSphere() {
        // Archimedes : z uniform in [-1, 1], angle uniform -> uniform on the sphere
        const float z = Range(-1.0f, 1.0f);
        const float angle = Range(0.0f, 6.28318530718f);
        const float r = std::sqrt(std::fmax(0.0f, 1.0f - z * z));
        return glm::vec3(r * std::cos(angle), r * std::sin(angle), z);
    }

    glm::vec2 Random::OnUnitCircle() {
        const float angle = Range(0.0f, 6.28318530718f);
        return glm::vec2(std::cos(angle), std::sin(angle));
    }
}

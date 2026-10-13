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
        // Rejection sampling, then the point is pushed out to the surface : only + - * / and sqrt (which IEEE 754 rounds
        // exactly), no sin / cos, whose last bit differs from one C library to the next and would break "the same seed gives
        // the same sequence on every platform". The points too close to the centre are skipped : their direction is mostly rounding.
        for (;;) {
            const glm::vec3 p(Range(-1.0f, 1.0f), Range(-1.0f, 1.0f), Range(-1.0f, 1.0f));
            const float lengthSquared = glm::dot(p, p);
            if (lengthSquared <= 1.0f && lengthSquared >= 1e-4f) return p / std::sqrt(lengthSquared);
        }
    }

    glm::vec2 Random::OnUnitCircle() {
        for (;;) {
            const glm::vec2 p(Range(-1.0f, 1.0f), Range(-1.0f, 1.0f));
            const float lengthSquared = glm::dot(p, p);
            if (lengthSquared <= 1.0f && lengthSquared >= 1e-4f) return p / std::sqrt(lengthSquared);
        }
    }
}

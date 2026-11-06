#include "engine/core/math/random.hpp"

#include <cmath>

namespace Shard::Engine::Core {

    // Draw components in separate statements, last first: constructor argument order is unspecified (GCC and Clang differ). Last-first keeps recorded sequences and replays valid.
    namespace {
        glm::vec2 RandomVec2(Random& r) {
            const float y = r.Range(-1.0f, 1.0f);
            const float x = r.Range(-1.0f, 1.0f);
            return { x, y };
        }

        glm::vec3 RandomVec3(Random& r) {
            const float z = r.Range(-1.0f, 1.0f);
            const float y = r.Range(-1.0f, 1.0f);
            const float x = r.Range(-1.0f, 1.0f);
            return { x, y, z };
        }
    }

    glm::vec2 Random::InUnitDisk() {
        for (;;) {
            const glm::vec2 p = RandomVec2(*this);
            if (glm::dot(p, p) <= 1.0f) return p;
        }
    }

    glm::vec3 Random::InUnitSphere() {
        for (;;) {
            const glm::vec3 p = RandomVec3(*this);
            if (glm::dot(p, p) <= 1.0f) return p;
        }
    }

    glm::vec3 Random::OnUnitSphere() {
        // Rejection sampling, then push to the surface. Only + - * / sqrt, no sin/cos, so results match on every platform.
        for (;;) {
            const glm::vec3 p = RandomVec3(*this);
            const float lengthSquared = glm::dot(p, p);
            if (lengthSquared <= 1.0f && lengthSquared >= 1e-4f) return p / std::sqrt(lengthSquared);
        }
    }

    glm::vec2 Random::OnUnitCircle() {
        for (;;) {
            const glm::vec2 p = RandomVec2(*this);
            const float lengthSquared = glm::dot(p, p);
            if (lengthSquared <= 1.0f && lengthSquared >= 1e-4f) return p / std::sqrt(lengthSquared);
        }
    }
}

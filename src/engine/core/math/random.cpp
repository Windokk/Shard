#include "engine/core/math/random.hpp"

#include <cmath>

namespace Shard::Engine::Core {

    // The components are drawn in separate statements, last one first : the arguments of a constructor have no defined
    // evaluation order (GCC went right to left, Clang goes left to right), so glm::vec3(Range(), Range(), Range()) gave
    // different vectors from the same seed depending on the compiler. Last-first is the order GCC used, which keeps the
    // sequences (and the pinned values / replays) recorded so far.
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
        // Rejection sampling : uniform, and the loop runs 1.27 times on average
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
        // Rejection sampling, then the point is pushed out to the surface : only + - * / and sqrt (which IEEE 754 rounds
        // exactly), no sin / cos, whose last bit differs from one C library to the next and would break "the same seed gives
        // the same sequence on every platform". The points too close to the centre are skipped : their direction is mostly rounding.
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

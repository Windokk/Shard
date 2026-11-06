#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/core/math/reference_frame.hpp"

namespace Shard::Engine::Rendering {

    // The render origin is subtracted in double before narrowing to float. Identity near the root origin.
    struct FrameToRender {
        bool identity = true;
        glm::dvec3 translation = glm::dvec3(0.0);
        glm::dquat rotation = glm::dquat(1.0, 0.0, 0.0, 0.0);
        bool rotates = false;

        glm::vec3 ApplyToPoint(const glm::vec3& p) const
        {
            if (identity)
                return p;
            const glm::dvec3 d = rotates ? rotation * glm::dvec3(p) : glm::dvec3(p);
            return glm::vec3(translation + d);
        }

        glm::vec3 ApplyToDirection(const glm::vec3& d) const
        {
            if (!rotates)
                return d;
            return glm::vec3(rotation * glm::dvec3(d));
        }

        glm::mat4 ApplyToMatrix(const glm::mat4& m) const
        {
            if (identity)
                return m;
            glm::mat4 out = m;
            if (rotates) {
                const glm::mat3 r = glm::mat3_cast(glm::quat(rotation));
                out = glm::mat4(r) * m;
            }
            const glm::dvec3 t = translation + (rotates ? rotation * glm::dvec3(m[3]) : glm::dvec3(m[3]));
            out[3] = glm::vec4(glm::vec3(t), m[3].w);
            return out;
        }

        glm::mat4 ToMatrix() const
        {
            glm::mat4 out = rotates ? glm::mat4_cast(glm::quat(rotation)) : glm::mat4(1.0f);
            out[3] = glm::vec4(glm::vec3(translation), 1.0f);
            return out;
        }
    };

    inline FrameToRender MakeFrameToRender(const Core::ReferenceFrameTree& tree, Core::FrameId frame, const Core::WorldPosition& origin)
    {
        FrameToRender result;
        if (frame == Core::kRootFrame && origin == Core::WorldPosition())
            return result;

        result.translation = Core::WorldPosition::Offset(tree.GetOriginInRoot(frame), origin);
        result.rotation = frame == Core::kRootFrame ? glm::dquat(1.0, 0.0, 0.0, 0.0) : tree.GetRotationInRoot(frame);
        result.rotates = result.rotation != glm::dquat(1.0, 0.0, 0.0, 0.0);
        result.identity = !result.rotates && result.translation == glm::dvec3(0.0);
        return result;
    }
}

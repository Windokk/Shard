#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/core/math/reference_frame.hpp"

namespace Shard::Engine::Rendering {

    /// @brief How what lives in one reference frame is brought into render space : floats measured from the render origin.
    ///
    /// The Extract stage turns every position, matrix and view it copies into the snapshot with one of these (one per frame
    /// that is used). The subtraction of the render origin is done in double, on exact WorldPositions, BEFORE the narrowing
    /// to float : an object a hundred million kilometres from the root, next to the camera, is drawn with the precision of its distance
    /// to the camera. The renderer only ever sees the small floats, so it is unchanged.
    ///
    /// When the frame is the root and the render origin is the root's origin, the transform is the identity and every Apply*
    /// returns its input untouched (bit for bit) : a scene that stays near the origin extracts exactly what it did before.
    struct FrameToRender {
        bool identity = true;
        glm::dvec3 translation = glm::dvec3(0.0);       ///< the frame's origin, relative to the render origin
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

        /// A matrix that maps to the frame's space -> the same matrix mapping to render space (the translation is composed in double)
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

        /// The frame's own placement as a float matrix : the correction to put in front of a matrix in the frame's space
        glm::mat4 ToMatrix() const
        {
            glm::mat4 out = rotates ? glm::mat4_cast(glm::quat(rotation)) : glm::mat4(1.0f);
            out[3] = glm::vec4(glm::vec3(translation), 1.0f);
            return out;
        }
    };

    /// @brief The FrameToRender of `frame` against the render origin `origin` (a position of the root frame)
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

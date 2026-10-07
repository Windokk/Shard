#pragma once

#include <array>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering {

    /// Six clip planes (right, left, top, bottom, near, far), each as (normal.xyz, d) with a unit normal :
    /// a point p is on the inside of a plane when dot(normal, p) + d >= 0.
    using FrustumPlanes = std::array<glm::vec4, 6>;

    /// Gribb-Hartmann extraction from a view-projection matrix using OpenGL clip conventions (-w <= z <= w).
    /// Works for perspective and orthographic matrices alike, so a shadow light matrix can be culled against
    /// exactly like a camera.
    inline FrustumPlanes ExtractFrustumPlanes(const glm::mat4& m)
    {
        FrustumPlanes planes = {
            glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0], m[3][3] - m[3][0]), // right
            glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0], m[3][3] + m[3][0]), // left
            glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1], m[3][3] - m[3][1]), // top
            glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1], m[3][3] + m[3][1]), // bottom
            glm::vec4(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2], m[3][3] + m[3][2]), // near
            glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2], m[3][3] - m[3][2]), // far
        };

        for (glm::vec4& plane : planes)
            plane /= glm::length(glm::vec3(plane));

        return planes;
    }

    /// False when the axis-aligned box lies entirely outside at least one plane. Conservative : a box that
    /// straddles a corner of the frustum without touching it still counts as inside.
    inline bool AABBInFrustum(const FrustumPlanes& planes, const glm::vec3& boundsMin, const glm::vec3& boundsMax)
    {
        for (const glm::vec4& plane : planes)
        {
            // The box corner furthest along the plane's normal : if even that one is behind the plane, the whole box is.
            glm::vec3 positiveVertex(
                plane.x >= 0.0f ? boundsMax.x : boundsMin.x,
                plane.y >= 0.0f ? boundsMax.y : boundsMin.y,
                plane.z >= 0.0f ? boundsMax.z : boundsMin.z);

            if (glm::dot(glm::vec3(plane), positiveVertex) + plane.w < 0.0f)
                return false;
        }

        return true;
    }
}

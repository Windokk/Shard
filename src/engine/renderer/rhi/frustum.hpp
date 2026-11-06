#pragma once

#include <array>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering {

    // Planes (right, left, top, bottom, near, far) as (normal.xyz, d) with unit normal. Inside when dot(normal, p) + d >= 0.
    using FrustumPlanes = std::array<glm::vec4, 6>;

    // Gribb-Hartmann extraction, OpenGL clip conventions (-w <= z <= w). Works for ortho too, so shadow matrices cull like cameras.
    inline FrustumPlanes ExtractFrustumPlanes(const glm::mat4& m)
    {
        FrustumPlanes planes = {
            glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0], m[3][3] - m[3][0]),
            glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0], m[3][3] + m[3][0]),
            glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1], m[3][3] - m[3][1]),
            glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1], m[3][3] + m[3][1]),
            glm::vec4(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2], m[3][3] + m[3][2]),
            glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2], m[3][3] - m[3][2]),
        };

        for (glm::vec4& plane : planes)
            plane /= glm::length(glm::vec3(plane));

        return planes;
    }

    inline bool AABBInFrustum(const FrustumPlanes& planes, const glm::vec3& boundsMin, const glm::vec3& boundsMax)
    {
        for (const glm::vec4& plane : planes)
        {
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

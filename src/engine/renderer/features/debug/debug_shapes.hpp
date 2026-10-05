#pragma once

#include "engine/renderer/utils.hpp"

#include <memory>
#include <vector>

namespace Shard::Engine::Rendering {

    class Mesh;

    class DebugShape
    {
        public:

            DebugShape() = default;
            virtual ~DebugShape();

            std::shared_ptr<Mesh> m_Mesh;
    };

    class DebugBox : public DebugShape
    {
        public:

            DebugBox(glm::vec3 halfExtent, COL_RGBA color)
            {
                GenerateBox(halfExtent, color);
            }

        private:

            void GenerateBox(glm::vec3 halfExtent, COL_RGBA color);
    };

    class DebugSphere : public DebugShape
    {
        public:

            DebugSphere(float radius, COL_RGBA color)
            {
                GenerateSphere(radius, color);
            }

        private:

            void GenerateSphere(float radius, COL_RGBA color);
    };

    class DebugCapsule : public DebugShape
    {
        public:

            DebugCapsule(float radius, float halfHeight, COL_RGBA color)
            {
                GenerateCapsule(radius, halfHeight, color);
            }
        private:
            void GenerateCapsule(float radius, float halfHeight, COL_RGBA color);
    };

    class DebugCylinder : public DebugShape
    {
        public:

            DebugCylinder(float radius, float halfHeight, COL_RGBA color)
            {
                GenerateCylinder(radius, halfHeight, color);
            }

        private:
            void GenerateCylinder(float radius, float halfHeight, COL_RGBA color);
    };

    // A single merged wireframe mesh containing one small sphere per entry in `centers` (local-space,
    // relative to whatever transform the owning component submits its draw command with). Used to
    // visualize a cluster of points (e.g. a GI probe grid) as one draw command instead of one per point.
    class DebugMultiSphere : public DebugShape
    {
        public:

            DebugMultiSphere(const std::vector<glm::vec3>& centers, float radius, COL_RGBA color)
            {
                GenerateMultiSphere(centers, radius, color);
            }

        private:
            void GenerateMultiSphere(const std::vector<glm::vec3>& centers, float radius, COL_RGBA color);
    };
}
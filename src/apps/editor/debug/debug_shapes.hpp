#pragma once

#include "engine/core/color.hpp"

#include <memory>
#include <vector>

namespace Shard::Engine::Rendering {
    class Mesh;
}

namespace Shard::Editor::Debug {

    class DebugShape
    {
        public:

            DebugShape() = default;
            virtual ~DebugShape();

            std::shared_ptr<Engine::Rendering::Mesh> m_Mesh;
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

    class DebugVolumeShape : public DebugShape
    {
        public:

            enum class Kind { Sphere, Cylinder, Cone, Pyramid };

            DebugVolumeShape(Kind kind, glm::vec3 halfExtent, COL_RGBA color)
            {
                Generate(kind, halfExtent, color);
            }

        private:
            void Generate(Kind kind, glm::vec3 halfExtent, COL_RGBA color);
    };

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
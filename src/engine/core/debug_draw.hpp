#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "engine/core/color.hpp"

namespace Shard::Engine::Core{

    enum class DebugShapeKind { Sphere, Box, Capsule, Cylinder };

    struct DebugShapeDesc{
        DebugShapeKind kind = DebugShapeKind::Box;
        float radius = 0.0f;          // sphere, capsule, cylinder
        float halfHeight = 0.0f;      // capsule, cylinder
        glm::vec3 halfExtent = glm::vec3(0.0f); // box
        COL_RGBA color = COL_RGBA(0.0f, 1.0f, 1.0f, 1.0f);
    };

    /// Identifies a shape created by an IDebugDraw (0 : none)
    using DebugShapeHandle = uint32_t;

    /// Which family of debug drawings a shape belongs to (the implementation decides how each is shown or hidden)
    enum class DebugDrawLayer { Physics };

    struct DebugDrawItem{
        DebugShapeHandle shape = 0;
        glm::mat4 model = glm::mat4(1.0f);
        uint32_t objectID = 0;
        /// Identifies who draws it (a component id in its world) : two owners drawing the same shape do not overwrite each other
        uint32_t ownerID = 0;
    };

    /// @brief What the modules that want to show debug geometry (the physics colliders...) draw through. The
    /// renderer implements it : they know no mesh, material nor draw command.
    class IDebugDraw{
        public:
            virtual ~IDebugDraw() = default;

            virtual DebugShapeHandle CreateShape(const DebugShapeDesc& desc) = 0;

            /// Removes whatever the shape drew, and frees it
            virtual void DestroyShape(DebugShapeHandle shape) = 0;

            /// Draws (or moves, when already drawn) the shapes of one owner, in the order given
            virtual void Draw(DebugDrawLayer layer, const std::vector<DebugDrawItem>& items) = 0;
    };
}

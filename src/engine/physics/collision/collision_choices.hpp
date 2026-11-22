#pragma once

namespace Shard::Engine::Physics
{
    enum class CollisionModeChoice : int {
        Inherit = 0,
        Solid,
        Sensor,
        QueryOnly,
        Disabled
    };

    enum class CollisionResponseChoice : int {
        Inherit = 0,
        Ignore,
        Overlap,
        Block
    };
}

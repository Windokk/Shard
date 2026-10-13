#pragma once

#include <cstdint>
#include <string>

namespace Shard::Engine::Core {

    class IEngineContext;

    /// @brief What a run looks like at one moment, as hashes : the value two runs compare to say they stayed the same.
    /// One hash per kind of state, so that when a run diverges the first difference says where to look.
    struct SimulationStateHash {
        uint64_t frame = 0;             ///< frames run so far
        uint64_t physicsSteps = 0;      ///< fixed steps the physics simulated so far
        uint64_t transforms = 0;        ///< position, rotation and scale of every actor, world by world
        uint64_t physics = 0;           ///< pose, velocities and activity of every physics body, as Jolt has them
        uint64_t scripts = 0;           ///< the reflected fields of every script (the game's own state)
        uint64_t total = 0;             ///< all of the above

        /// "total=... (transforms=..., physics=..., scripts=...)" : for the logs
        std::string Describe() const;
    };

    /// @brief Hashes the state of the simulation : the worlds that are loaded, their actors' transforms, the physics bodies,
    /// the scripts' fields. Everything is visited in a fixed order (by id, never by the order of a hash table), and the
    /// floats go in as their IEEE bits (see Determinism::StateHasher), so the value is the same on every platform when the
    /// run was. It reads only : call it between frames (after Run), from the thread that runs the engine.
    SimulationStateHash HashSimulationState(IEngineContext& engine);
}

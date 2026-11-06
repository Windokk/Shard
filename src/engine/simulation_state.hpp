#pragma once

#include <cstdint>
#include <string>

namespace Shard::Engine::Core {

    class IEngineContext;

    struct SimulationStateHash {
        uint64_t frame = 0;
        uint64_t physicsSteps = 0;
        uint64_t transforms = 0;
        uint64_t physics = 0;
        uint64_t scripts = 0;
        uint64_t total = 0;

        std::string Describe() const;
    };

    // Fixed visit order (by id), floats as IEEE bits, so the value is the same on every platform. Call between frames, from the engine thread.
    SimulationStateHash HashSimulationState(IEngineContext& engine);
}

#pragma once

#include "engine/core/math/random.hpp"
#include "engine/world/components/script.hpp"
#include "engine/world/components/registry/component_macros.hpp"

using namespace nlohmann;

/// @brief Kicks the physics body of its actor in a random direction every `interval` frames. The reference for gameplay that
/// needs random numbers : it takes its generator from the engine (a stream of its own, derived from the run's seed) instead of
/// rand() / std::random_device, so a replay of the run kicks the same way. The determinism tests use it for that.
class CLASS() RandomKicker : public Shard::Engine::Objects::Components::Script{

    public:
        RandomKicker(std::shared_ptr<Actor> parent, uint32_t local_id) : Script(parent, local_id){};

        void OnPlay() override;
        void OnTick() override;

        /// Frames between two kicks
        FIELD(Editable)
        int interval = 30;

        /// Impulse of a kick (mass * units per second)
        FIELD(Editable)
        float strength = 4.0f;

        FIELD(ReadOnly)
        int frames = 0;

        FIELD(ReadOnly)
        int kicks = 0;

    private:

        Shard::Engine::Core::Random m_Random;

        DECLARE_DESCRIPTOR(RandomKicker);
};

DECLARE_COMPONENT(RandomKicker)

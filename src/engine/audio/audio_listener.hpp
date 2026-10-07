#pragma once

#include "engine/world/components/component.hpp"

#include "engine/assets/reflection/attributes.hpp"

namespace Shard::Engine::Objects::Components
{
    /// @brief Where the world is heard from : the audio plays every AudioSource relative to the position and
    /// facing of the actor that carries the first active AudioListener of the world.
    class CLASS() AudioListener : public Component {

        public:
            AudioListener(std::shared_ptr<Actor> parent, uint32_t local_id);


            ordered_json Serialize() override;

            std::shared_ptr<Component> Clone() const override;

            DECLARE_DESCRIPTOR(AudioListener)
    };
}

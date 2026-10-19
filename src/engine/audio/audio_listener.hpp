#pragma once

#include "engine/world/components/component.hpp"

#include "engine/assets/reflection/reflection_fields.hpp"
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

            FIELD(Editable, range=0.0f|5.0f, tooltip=Scales the doppler effect of every sound heard from here. 0 turns it off)
            float dopplerFactor = 1.0f;

            FIELD(Editable, range=1.0f|100000.0f, tooltip=Speed of sound in world units per second (343 for the air at 20 degrees, in a project in metres). Lower it to hear the doppler effect at slower speeds)
            float speedOfSound = 343.0f;

            DECLARE_DESCRIPTOR(AudioListener)
    };
}

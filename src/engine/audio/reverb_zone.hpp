#pragma once

#include <glm/glm.hpp>

#include "engine/audio/audio_effects.hpp"

#include "engine/world/components/component.hpp"

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/assets/reflection/attributes.hpp"

namespace Shard::Engine::Objects::Components
{
    class CLASS() ReverbZone : public Component {
        public:
            ReverbZone(std::shared_ptr<Actor> parent, uint32_t local_id);

            void Deserialize(const json componentData) override;
            ordered_json Serialize() override;
            std::shared_ptr<Component> Clone() const override;

            void OnFieldChanged(const FieldChangedEvent& event) override;

            Audio::ReverbParams GetParams() const;

            float SampleWeight(const glm::dvec3& position);

            FIELD(Editable, tooltip=Shape of the region the zone covers. Box follows the rotation of the actor)
            Audio::ReverbShape shape = Audio::ReverbShape::Box;

            FIELD(Editable, tooltip=Box only : half of the size of the zone along each axis, before the scale of the actor)
            glm::vec3 halfExtent = glm::vec3(5.0f);

            FIELD(Editable, range=0.0f|100000.0f, tooltip=Sphere only : radius of the zone, before the scale of the actor)
            float radius = 5.0f;

            FIELD(Editable, range=0.0f|10000.0f, tooltip=The reverb fades in over this distance outside the edge of the zone, so entering it is not abrupt. 0 is a hard edge)
            float blendDistance = 2.0f;

            FIELD(Editable, range=-100|100, tooltip=The zone of the highest priority that covers the listener wins over the lower ones. Zones of the same priority blend)
            int priority = 0;

            FIELD(Editable, tooltip=A ready-made room. Picking one fills the settings below, changing one of them goes back to Custom)
            Audio::ReverbPreset preset = Audio::ReverbPreset::Generic;

            FIELD(Editable, range=0.0f|1.0f, tooltip=Size of the room : the larger, the longer the tail of the reverb)
            float roomSize = 0.5f;

            FIELD(Editable, range=0.0f|1.0f, tooltip=How fast the high frequencies die in the tail. Soft materials (carpet, curtains) damp a lot, stone and tiles do not)
            float damping = 0.3f;

            FIELD(Editable, range=0.0f|1.0f, tooltip=Stereo spread of the tail. 0 is mono)
            float width = 1.0f;

            FIELD(Editable, range=0.0f|2.0f, tooltip=Level of the reverberated sound, relative to the sound that goes into it)
            float wetLevel = 0.45f;

            FIELD(Editable, range=0.0f|1.0f, tooltip=Level of the direct sound while the listener is in the zone. Lower it to push the sound back into the room)
            float dryLevel = 0.9f;

            FIELD(Editable, range=0.0f|250.0f, tooltip=Delay before the reverb starts, in milliseconds. Gives the size of the room to the ear)
            float preDelayMs = 10.0f;

            FIELD(Editable, range=10.0f|2000.0f, tooltip=The reverberated sound has nothing below this frequency (Hz). Keeps the tail from getting muddy)
            float lowCutHz = 20.0f;

            FIELD(Editable, range=500.0f|20000.0f, tooltip=The reverberated sound has nothing above this frequency (Hz). Lower it for a duller room)
            float highCutHz = 12000.0f;

            DECLARE_DESCRIPTOR(ReverbZone)
    };
}

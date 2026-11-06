#include "reverb_zone.hpp"

#include <cstring>

#include <glm/gtc/quaternion.hpp>

#include "engine/world/actor.hpp"
#include "engine/world/components/transform.hpp"

#include "reverb_zone.reflection.hpp"

namespace Shard::Engine::Objects::Components{

    ReverbZone::ReverbZone(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {
    }

    void ReverbZone::Deserialize(const json componentData)
    {
        DeserializeReflectedFields(componentData, false);
        DeserializeActive(componentData, true);
    }

    ordered_json ReverbZone::Serialize()
    {
        return SerializeReflected("audio_reverb_zone");
    }

    std::shared_ptr<Component> ReverbZone::Clone() const
    {
        return Object::Create<ReverbZone>(*this);
    }

    void ReverbZone::OnFieldChanged(const FieldChangedEvent& event)
    {
        if (std::strcmp(event.field->name, "preset") == 0)
        {
            if (preset != Audio::ReverbPreset::Custom)
            {
                const Audio::ReverbParams values = Audio::GetReverbPreset(preset);
                roomSize = values.roomSize;
                damping = values.damping;
                width = values.width;
                wetLevel = values.wetLevel;
                dryLevel = values.dryLevel;
                preDelayMs = values.preDelayMs;
                lowCutHz = values.lowCutHz;
                highCutHz = values.highCutHz;
            }
            return;
        }

        static const char* const kSettings[] = { "roomSize", "damping", "width", "wetLevel", "dryLevel", "preDelayMs", "lowCutHz", "highCutHz" };
        for (const char* name : kSettings)
        {
            if (std::strcmp(event.field->name, name) == 0)
            {
                preset = Audio::ReverbPreset::Custom;
                return;
            }
        }
    }

    Audio::ReverbParams ReverbZone::GetParams() const
    {
        Audio::ReverbParams params;
        params.roomSize = roomSize;
        params.damping = damping;
        params.width = width;
        params.wetLevel = wetLevel;
        params.dryLevel = dryLevel;
        params.preDelayMs = preDelayMs;
        params.lowCutHz = lowCutHz;
        params.highCutHz = highCutHz;
        return params;
    }

    float ReverbZone::SampleWeight(const glm::dvec3& position)
    {
        if (!parent || !parent->transform)
            return 0.0f;

        const glm::dvec3 center = parent->transform->GetWorldPositionD();
        const glm::dquat rotation = parent->transform->GetWorldRotationD();
        const glm::vec3 scale = glm::abs(parent->transform->GetWorldScale());

        const glm::vec3 local = glm::vec3(glm::inverse(rotation) * (position - center));

        const float largestScale = std::max(scale.x, std::max(scale.y, scale.z));
        return Audio::ReverbZoneWeight(shape, local, halfExtent * scale, radius * largestScale, blendDistance);
    }
}

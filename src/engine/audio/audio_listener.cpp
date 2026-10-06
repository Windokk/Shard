#include "audio_listener.hpp"

#include "engine/world/actor.hpp"

#include "audio_listener.reflection.hpp"

namespace Shard::Engine::Objects::Components
{
    AudioListener::AudioListener(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {
    }

    void AudioListener::Deserialize(const json componentData)
    {
        if (componentData.contains("active") && componentData["active"].is_boolean() && !componentData["active"].get<bool>())
            DeActivate();
        else
            Activate();
    }

    ordered_json AudioListener::Serialize()
    {
        ordered_json comp;

        comp["type"] = "audio_listener";

        comp["active"] = activated;

        return comp;
    }

    std::shared_ptr<Component> AudioListener::Clone() const
    {
        return Object::Create<AudioListener>(*this);
    }
}

#include "audio_listener.hpp"

#include "engine/world/actor.hpp"

#include "audio_listener.reflection.hpp"

namespace Shard::Engine::Objects::Components
{
    AudioListener::AudioListener(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {
    }

    ordered_json AudioListener::Serialize()
    {
        return SerializeReflected("audio_listener");
    }

    std::shared_ptr<Component> AudioListener::Clone() const
    {
        return Object::Create<AudioListener>(*this);
    }
}

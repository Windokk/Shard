#include "volume.hpp"

#include "engine/world/actor.hpp"

namespace Shard::Engine::Objects::Components{

    Volume::Volume(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {
    }

}

#include "component.hpp"

#include "engine/core/diagnostics/logger.hpp"
#include "engine/world/actor.hpp"

namespace Shard::Engine::Objects::Components {

    Component::Component(std::shared_ptr<Actor> parent, uint32_t local_id)
    {
        this->parent = parent;
        this->local_id = local_id;
    }

    Component::~Component()
    {
    }

    Core::IEngineContext* Component::GetEngineContext() const
    {
        return parent ? parent->GetEngineContext() : nullptr;
    }
}
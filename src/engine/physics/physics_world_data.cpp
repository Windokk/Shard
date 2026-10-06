#include "physics_world_data.hpp"

#include "engine/physics/physics_body.hpp"
#include "engine/world/components/registry/component_registry.hpp"

namespace Shard::Engine::Physics{

    void PhysicsWorldData::OnComponentAdded(Worlds::World&, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool)
    {
        if(auto body = std::dynamic_pointer_cast<Objects::Components::PhysicsBody>(component))
            bodies.emplace(idInWorld, body);
    }

    void PhysicsWorldData::OnComponentRemoved(Worlds::World&, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component)
    {
        if(component->IsInstanceOf<Objects::Components::PhysicsBody>())
            bodies.erase(idInWorld);
    }

    void RegisterPhysicsModule()
    {
        Objects::Components::GetComponentRegistry().RegisterBuiltinComponent<Objects::Components::PhysicsBody>("physics_body");
        Worlds::RegisterWorldExtension<PhysicsWorldData>();
    }
}

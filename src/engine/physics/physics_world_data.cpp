#include "physics_world_data.hpp"

#include "engine/physics/physics_body.hpp"
#include "engine/physics/gravity_field.hpp"
#include "engine/world/components/registry/component_registry.hpp"

namespace Shard::Engine::Physics{

    void PhysicsWorldData::OnComponentAdded(Worlds::World&, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool)
    {
        if(auto body = std::dynamic_pointer_cast<Objects::Components::PhysicsBody>(component))
            bodies.emplace(idInWorld, body);
        else if(auto field = std::dynamic_pointer_cast<Objects::Components::GravityField>(component))
            gravityFields.emplace(idInWorld, field);
    }

    void PhysicsWorldData::OnComponentRemoved(Worlds::World&, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component)
    {
        if(component->IsInstanceOf<Objects::Components::PhysicsBody>())
            bodies.erase(idInWorld);
        else if(component->IsInstanceOf<Objects::Components::GravityField>())
            gravityFields.erase(idInWorld);
    }

    void RegisterPhysicsModule()
    {
        auto& registry = Objects::Components::GetComponentRegistry();
        registry.RegisterBuiltinComponent<Objects::Components::PhysicsBody>("physics_body");
        registry.RegisterBuiltinComponent<Objects::Components::UniformGravityField>("gravityUniform");
        registry.RegisterBuiltinComponent<Objects::Components::PointGravityField>("gravityPoint");
        registry.RegisterBuiltinComponent<Objects::Components::RadialGravityField>("gravityRadial");
        registry.RegisterBuiltinComponent<Objects::Components::GravityVolume>("gravityVolume");
        Worlds::RegisterWorldExtension<PhysicsWorldData>();
    }
}

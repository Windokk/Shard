#pragma once

#include <map>
#include <memory>

#include "engine/world/world_extension.hpp"

namespace Shard::Engine::Objects::Components{
    class PhysicsBody;
    class GravityField;
}

namespace Shard::Engine::Physics{

    /// What the physics keeps on a world : its bodies and its gravity fields, by id in the world. Ordered : they are updated in the order
    /// they were added, on every platform (see World::scripts).
    class PhysicsWorldData : public Worlds::IWorldExtension{
        public:
            std::map<int, std::shared_ptr<Objects::Components::PhysicsBody>> bodies;
            std::map<int, std::shared_ptr<Objects::Components::GravityField>> gravityFields;

            void OnComponentAdded(Worlds::World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool cloned) override;
            void OnComponentRemoved(Worlds::World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component) override;
    };

    /// Declares to the world what the physics adds to it : its component type and its world extension.
    void RegisterPhysicsModule();
}

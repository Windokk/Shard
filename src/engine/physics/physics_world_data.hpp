#pragma once

#include <map>
#include <memory>

#include "engine/world/world_extension.hpp"

namespace Shard::Engine::Objects::Components{
    class PhysicsBody;
}

namespace Shard::Engine::Physics{

    /// What the physics keeps on a world : its bodies, by id in the world. Ordered : the bodies are updated in the order they
    /// were added, on every platform (see World::scripts).
    class PhysicsWorldData : public Worlds::IWorldExtension{
        public:
            std::map<int, std::shared_ptr<Objects::Components::PhysicsBody>> bodies;

            void OnComponentAdded(Worlds::World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool cloned) override;
            void OnComponentRemoved(Worlds::World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component) override;
    };

    /// Declares to the world what the physics adds to it : its component type and its world extension.
    void RegisterPhysicsModule();
}

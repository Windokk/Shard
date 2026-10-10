#pragma once

namespace Shard::Engine::Worlds {

    /// @brief On the entity of every actor : the ObjectID of the actor it stands for.
    ///
    /// Actor / Component stay the API the editor, the scripts and the serialization use (the facade) ; each actor owns an entity of
    /// the engine's Ecs::Registry that mirrors it (its parent / children, and the data systems work on). A system that finds an
    /// entity through a query gets back to the actor with this link ; the actor reaches its entity with Actor::GetEntity().
    struct ActorLink {
        int objectId = -1;
    };
}

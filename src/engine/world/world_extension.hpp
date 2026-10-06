#pragma once

#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/assets/resources_manager.hpp"

namespace Shard::Engine::Objects::Components{
    class Component;
}

namespace Shard::Engine::Worlds{

    class World;

    /// @brief What a module keeps on a world. `world` owns the world and knows nothing about the renderer, the
    /// physics or the audio : each of them hangs its own data (the lights, the physics bodies, the skybox...) on
    /// the world through an extension, and is told when something happens to the world.
    /// Get one with `world.Ext<T>()` (created on first use), register it with RegisterWorldExtension<T>() so
    /// that it also takes part in the world's (de)serialization.
    class IWorldExtension{
        public:
            virtual ~IWorldExtension() = default;

            /// A component was added to an actor of the world. `cloned` : it is a copy of another actor's component.
            virtual void OnComponentAdded(World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool cloned) {}

            /// A component of the world is being removed (already destroyed).
            virtual void OnComponentRemoved(World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component) {}

            /// The world was loaded (every actor exists), before its scripts are told.
            virtual void OnLoad(World& world) {}

            /// The play mode starts, before the scripts are told.
            virtual void OnPlay(World& world) {}

            /// The `settings` object of the world file (read after the actors).
            virtual void DeserializeSettings(World& world, const nlohmann::json& settings) {}

            /// Fills the `settings` object of the world file.
            virtual void SerializeSettings(const World& world, nlohmann::ordered_json& settings) {}
    };

    using WorldExtensionFactory = std::function<std::unique_ptr<IWorldExtension>()>;

    void RegisterWorldExtension(std::type_index type, WorldExtensionFactory factory);

    /// Every registered extension, in registration order
    const std::vector<std::pair<std::type_index, WorldExtensionFactory>>& GetWorldExtensionFactories();

    template<class T>
    void RegisterWorldExtension()
    {
        RegisterWorldExtension(std::type_index(typeid(T)), []() -> std::unique_ptr<IWorldExtension> { return std::make_unique<T>(); });
    }

    /// Resources the `settings` object of a world file references (the skybox...), added by the module that reads them.
    using WorldSettingsAssetRefs = std::function<void(const nlohmann::json& settings, std::vector<Core::Resources::ResourceKey>& out)>;

    void RegisterWorldSettingsAssetRefs(WorldSettingsAssetRefs refs);

    const std::vector<WorldSettingsAssetRefs>& GetWorldSettingsAssetRefs();
}

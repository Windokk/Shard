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

    // A module hangs its own data on a world through an extension. Register with RegisterWorldExtension<T>() to join (de)serialization.
    class IWorldExtension{
        public:
            virtual ~IWorldExtension() = default;

            virtual void OnComponentAdded(World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool cloned) {}

            virtual void OnComponentRemoved(World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component) {}

            virtual void OnLoad(World& world) {}

            virtual void OnPlay(World& world) {}

            virtual void OnStop(World& world) {}

            virtual void OnUnload(World& world) {}

            virtual void DeserializeSettings(World& world, const nlohmann::json& settings) {}

            virtual void SerializeSettings(const World& world, nlohmann::ordered_json& settings) {}
    };

    using WorldExtensionFactory = std::function<std::unique_ptr<IWorldExtension>()>;

    void RegisterWorldExtension(std::type_index type, WorldExtensionFactory factory);

    bool UnregisterWorldExtension(std::type_index type);

    const std::vector<std::pair<std::type_index, WorldExtensionFactory>>& GetWorldExtensionFactories();

    template<class T>
    void RegisterWorldExtension()
    {
        RegisterWorldExtension(std::type_index(typeid(T)), []() -> std::unique_ptr<IWorldExtension> { return std::make_unique<T>(); });
    }

    using WorldJsonFilter = std::function<void(const std::string& pathInProject, nlohmann::json& world)>;
    uint32_t AddWorldJsonFilter(WorldJsonFilter filter);
    bool RemoveWorldJsonFilter(uint32_t id);
    void ApplyWorldJsonFilters(const std::string& pathInProject, nlohmann::json& world);

    using WorldSaveGuard = std::function<bool(const std::string& pathInProject)>;
    void SetWorldSaveGuard(WorldSaveGuard guard);
    bool AllowWorldSave(const std::string& pathInProject);

    using WorldSettingsAssetRefs = std::function<void(const nlohmann::json& settings, std::vector<Core::Resources::ResourceKey>& out)>;

    void RegisterWorldSettingsAssetRefs(WorldSettingsAssetRefs refs);

    uint32_t RegisterWorldSettingsAssetRefsWithId(WorldSettingsAssetRefs refs);
    bool UnregisterWorldSettingsAssetRefs(uint32_t id);

    const std::vector<WorldSettingsAssetRefs>& GetWorldSettingsAssetRefs();
}

#pragma once

#include <string>
#include <unordered_map>
#include <stdexcept>
#include <iostream>
#include <memory>
#include <functional>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/world/object.hpp"
#include "engine/assets/resources_manager.hpp"

namespace Shard::Engine::Objects {
    class Actor;
}

namespace Shard::Engine::Objects::Components {

    class Component;
    using ComponentFactory = std::function<std::shared_ptr<Component>()>;

    using BuiltinComponentFactory = std::function<std::shared_ptr<Component>(Core::IEngineContext*, std::shared_ptr<Actor>, uint32_t localId)>;

    using ComponentAssetRefs = std::function<void(const nlohmann::json& component, std::vector<Core::Resources::ResourceKey>& out)>;

    class ComponentRegistry {
        public:
            void RegisterComponentType(const std::string& name, ComponentFactory factory);
            std::shared_ptr<Component> CreateComponentByName(const std::string& name);

            bool UnregisterComponentType(const std::string& name);
            bool IsRegistered(const std::string& name) const;
            const std::unordered_map<std::string, ComponentFactory>& GetAll() const;

            template<class T>
            void RegisterBuiltinComponent(const std::string& typeName, ComponentAssetRefs assetRefs = nullptr)
            {
                BuiltinComponentFactory factory = [](Core::IEngineContext* engine, std::shared_ptr<Actor> actor, uint32_t localId) -> std::shared_ptr<Component> {
                    return Core::Object::CreateWithContext<T>(engine, std::move(actor), localId);
                };
                RegisterBuiltin(typeName, std::move(factory), std::move(assetRefs));
            }

            std::shared_ptr<Component> CreateBuiltinComponent(const std::string& typeName, Core::IEngineContext* engine, std::shared_ptr<Actor> actor, uint32_t localId);

            bool IsBuiltinComponent(const std::string& typeName) const;
            std::vector<std::string> BuiltinNames() const;

            void CollectAssetRefs(const std::string& typeName, const nlohmann::json& component, std::vector<Core::Resources::ResourceKey>& out) const;

            ComponentRegistry() = default;
            ComponentRegistry(const ComponentRegistry&) = delete;
            ComponentRegistry& operator=(const ComponentRegistry&) = delete;

        private:
            void RegisterBuiltin(const std::string& typeName, BuiltinComponentFactory factory, ComponentAssetRefs assetRefs);

        struct Builtin {
            BuiltinComponentFactory factory;
            ComponentAssetRefs assetRefs;
        };

        std::unordered_map<std::string, ComponentFactory> registry;
        std::unordered_map<std::string, Builtin> builtins;
    };

    extern ComponentRegistry gSharedComponentRegistry;

#if defined(BUILD_ENGINE)

    inline ComponentRegistry& GetComponentRegistry() {
        return gSharedComponentRegistry;
    }

#elif defined(BUILD_GAME)

    inline ComponentRegistry* gSharedComponentRegistryPtr = nullptr;

    inline void SetComponentRegistry(ComponentRegistry* ptr) {
        gSharedComponentRegistryPtr = ptr;
    }

    inline ComponentRegistry& GetComponentRegistry() {
        if (!gSharedComponentRegistryPtr)
            std::cout<<"ComponentRegistry pointer not initialized!"<<std::endl;
        return *gSharedComponentRegistryPtr;
    }

#endif

}
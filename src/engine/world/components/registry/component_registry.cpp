#include "component_registry.hpp"

#include <iostream>

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Objects::Components {

    ComponentRegistry gSharedComponentRegistry;

    void ComponentRegistry::RegisterComponentType(const std::string& name, ComponentFactory factory) {
        if (registry.find(name) != registry.end()) {
            std::cerr<<"Component already registered: " + name<<std::endl;
        }
        registry[name] = factory;
        std::cout<<"Registered custom component : " + name<<std::endl;
    }

    std::shared_ptr<Component> ComponentRegistry::CreateComponentByName(const std::string& name) {
        auto it = registry.find(name);
        if (it == registry.end()) {
            DEBUG_WARNING("Component not registered: " + name);
            return nullptr;
        }
        return it->second();
    }

    void ComponentRegistry::RegisterBuiltin(const std::string& typeName, BuiltinComponentFactory factory, ComponentAssetRefs assetRefs) {
        builtins[typeName] = Builtin{ std::move(factory), std::move(assetRefs) };
    }

    std::shared_ptr<Component> ComponentRegistry::CreateBuiltinComponent(const std::string& typeName, Core::IEngineContext* engine, std::shared_ptr<Actor> actor, uint32_t localId) {
        auto it = builtins.find(typeName);
        if (it == builtins.end())
            return nullptr;
        return it->second.factory(engine, std::move(actor), localId);
    }

    bool ComponentRegistry::IsBuiltinComponent(const std::string& typeName) const {
        return builtins.find(typeName) != builtins.end();
    }

    void ComponentRegistry::CollectAssetRefs(const std::string& typeName, const nlohmann::json& component, std::vector<Core::Resources::ResourceKey>& out) const {
        auto it = builtins.find(typeName);
        if (it != builtins.end() && it->second.assetRefs)
            it->second.assetRefs(component, out);
    }

    const std::unordered_map<std::string, ComponentFactory>& ComponentRegistry::GetAll() const {
        return registry;
    }
}
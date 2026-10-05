#include "component_registry.hpp"

#include <iostream>

#include "engine/debugging/logger.hpp"

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

    const std::unordered_map<std::string, ComponentFactory>& ComponentRegistry::GetAll() const {
        return registry;
    }
}
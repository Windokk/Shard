#include "component_registry.hpp"

#include <algorithm>
#include <iostream>

#include "engine/core/diagnostics/logger.hpp"
#include "engine/world/components/component.hpp"

namespace Shard::Engine::Objects::Components {

    ComponentRegistry gSharedComponentRegistry;

    void ComponentRegistry::RegisterComponentType(const std::string& name, ComponentFactory factory) {
        if (registry.find(name) != registry.end()) {
            std::cerr<<"Component already registered: " + name<<std::endl;
        }
        registry[name] = std::move(factory);
        std::cout<<"Registered custom component : " + name<<std::endl;
    }

    bool ComponentRegistry::UnregisterComponentType(const std::string& name) {
        return registry.erase(name) > 0;
    }

    bool ComponentRegistry::IsRegistered(const std::string& name) const {
        return registry.find(name) != registry.end();
    }

    std::shared_ptr<Component> ComponentRegistry::CreateComponentByName(const std::string& name) {
        auto it = registry.find(name);
        if (it == registry.end()) {
            DEBUG_WARNING("Component not registered: " + name);
            return nullptr;
        }
        return it->second();
    }

    void ComponentRegistry::RegisterBuiltin(const std::string& typeName, BuiltinComponentFactory factory, ComponentAssetRefs assetRefs, const ClassDescriptor* descriptor) {
        builtins[typeName] = Builtin{ std::move(factory), std::move(assetRefs), descriptor };
    }

    const ClassDescriptor* ComponentRegistry::DescriptorOf(const std::string& typeName) const {
        auto builtin = builtins.find(typeName);
        if (builtin != builtins.end())
            return builtin->second.descriptor;
        auto custom = registry.find(typeName);
        if (custom == registry.end() || !custom->second)
            return nullptr;
        try {
            std::shared_ptr<Component> instance = custom->second();
            return instance ? instance->GetDescriptor() : nullptr;
        } catch (...) {
            return nullptr;
        }
    }

    std::vector<std::string> ComponentRegistry::AllNames() const {
        std::vector<std::string> names = BuiltinNames();
        for (const auto& kv : registry) names.push_back(kv.first);
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        return names;
    }

    std::shared_ptr<Component> ComponentRegistry::CreateBuiltinComponent(const std::string& typeName, Core::IEngineContext* engine, std::shared_ptr<Actor> actor, uint32_t localId) {
        auto it = builtins.find(typeName);
        if (it == builtins.end())
            return nullptr;
        return it->second.factory(engine, std::move(actor), localId);
    }

    std::vector<std::string> ComponentRegistry::BuiltinNames() const {
        std::vector<std::string> names;
        names.reserve(builtins.size());
        for (const auto& kv : builtins) names.push_back(kv.first);
        std::sort(names.begin(), names.end());
        return names;
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
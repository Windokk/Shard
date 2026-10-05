#pragma once

#include <string>
#include <unordered_map>
#include <stdexcept>
#include <iostream>
#include <memory>

namespace Shard::Engine::Objects::Components {

    class Component;
    using ComponentFactory = std::shared_ptr<Component>(*)();

    class ComponentRegistry {
        public:
            void RegisterComponentType(const std::string& name, ComponentFactory factory);
            std::shared_ptr<Component> CreateComponentByName(const std::string& name);
            const std::unordered_map<std::string, ComponentFactory>& GetAll() const;

            ComponentRegistry() = default;
            ComponentRegistry(const ComponentRegistry&) = delete;
            ComponentRegistry& operator=(const ComponentRegistry&) = delete;

        private:
            std::unordered_map<std::string, ComponentFactory> registry;
    };

    extern ComponentRegistry gSharedComponentRegistry;

#if defined(BUILD_ENGINE)

    // Used by the EXE/engine
    inline ComponentRegistry& GetComponentRegistry() {
        return gSharedComponentRegistry;
    }

#elif defined(BUILD_GAME)

    // Used by the DLL
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
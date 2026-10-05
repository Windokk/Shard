#pragma once

#include "engine/world/components/registry/component_registry.hpp"

#include <vector>
#include <functional>
#include <iostream>

namespace Shard::Engine::Objects::Components {

    using RegisterComponentCallback = std::function<void(ComponentRegistry&)>;

    inline std::vector<RegisterComponentCallback>& GetComponentRegistrars() {
        static std::vector<RegisterComponentCallback> registrars;
        return registrars;
    }

    inline void AddComponentRegistrar(RegisterComponentCallback cb) {
        GetComponentRegistrars().push_back(std::move(cb));
    }

}
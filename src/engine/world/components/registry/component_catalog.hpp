#pragma once

#include <nlohmann/json.hpp>

#include "engine/world/components/registry/component_registry.hpp"

namespace Shard::Engine::Objects::Components {

    nlohmann::ordered_json DescribeComponentType(const ComponentRegistry& registry, const std::string& typeName);

    nlohmann::ordered_json DescribeComponentCatalog(const ComponentRegistry& registry);
}

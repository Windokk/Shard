#include "component_catalog.hpp"

#include "engine/assets/serialization/reflection/reflection_schema.hpp"
#include <algorithm>

#include "engine/world/components/transform.hpp"

namespace Shard::Engine::Objects::Components {

    nlohmann::ordered_json DescribeComponentType(const ComponentRegistry& registry, const std::string& typeName)
    {
        const ClassDescriptor* descriptor = typeName == "transform" ? &Transform::descriptor : registry.DescriptorOf(typeName);
        nlohmann::ordered_json out;
        if (descriptor) {
            out = Serialization::DescribeClass(*descriptor);
            out["type"] = typeName;
            out["builtin"] = registry.IsBuiltinComponent(typeName) || typeName == "transform";
        }
        else {
            out["name"] = typeName;
            out["type"] = typeName;
            out["builtin"] = registry.IsBuiltinComponent(typeName);
            out["fields"] = nlohmann::ordered_json::array();
            out["described"] = false;
        }
        return out;
    }

    nlohmann::ordered_json DescribeComponentCatalog(const ComponentRegistry& registry)
    {
        std::vector<std::string> names = registry.AllNames();
        names.push_back("transform");
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        nlohmann::ordered_json out = nlohmann::ordered_json::array();
        for (const std::string& name : names)
            out.push_back(DescribeComponentType(registry, name));
        return out;
    }
}

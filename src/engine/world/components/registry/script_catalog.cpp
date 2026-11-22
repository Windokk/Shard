#include "script_catalog.hpp"

namespace Shard::Engine::Objects::Components {

    namespace {
        const char* FieldTypeName(Events::NamedEventLayout::FieldType type)
        {
            using T = Events::NamedEventLayout::FieldType;
            switch (type) {
                case T::F32: return "float";
                case T::F64: return "double";
                case T::I32: return "int32";
                case T::U32: return "uint32";
                case T::I64: return "int64";
                case T::U64: return "uint64";
                case T::Bool: return "bool";
                case T::Vec2: return "vec2";
                case T::Vec3: return "vec3";
                case T::Vec4: return "vec4";
                case T::Quat: return "quat";
                case T::Color: return "color_rgba";
            }
            return "unknown";
        }
    }

    nlohmann::ordered_json DescribeCommands(const Core::CVarRegistry& registry)
    {
        nlohmann::ordered_json out = nlohmann::ordered_json::array();
        for (const auto& command : registry.ListCommands()) {
            nlohmann::ordered_json item;
            item["name"] = command.name;
            item["description"] = command.description.empty() ? command.help : command.description;
            nlohmann::ordered_json params = nlohmann::ordered_json::array();
            for (const auto& param : command.params)
                params.push_back({ { "name", param.name }, { "optional", param.optional }, { "variadic", param.variadic } });
            item["params"] = params;
            out.push_back(std::move(item));
        }
        return out;
    }

    nlohmann::ordered_json DescribeEvents(const Events::EventDispatcher& dispatcher)
    {
        nlohmann::ordered_json out = nlohmann::ordered_json::array();
        for (const std::string& name : dispatcher.describedNames()) {
            const Events::NamedEventLayout* layout = dispatcher.namedLayout(name);
            if (!layout) continue;
            nlohmann::ordered_json item;
            item["name"] = name;
            item["size"] = layout->size;
            nlohmann::ordered_json fields = nlohmann::ordered_json::array();
            for (const auto& field : layout->fields)
                fields.push_back({ { "name", field.name }, { "type", FieldTypeName(field.type) }, { "offset", field.offset } });
            item["fields"] = fields;
            out.push_back(std::move(item));
        }
        return out;
    }
}

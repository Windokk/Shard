#include "reflection_schema.hpp"

#include <cstring>

namespace Shard::Engine::Serialization
{
    const char* TypeName(TypeID type)
    {
        switch (type)
        {
            case TypeID::Int8: return "int8";
            case TypeID::Int16: return "int16";
            case TypeID::Int32: return "int32";
            case TypeID::Int64: return "int64";
            case TypeID::UInt8: return "uint8";
            case TypeID::UInt16: return "uint16";
            case TypeID::UInt32: return "uint32";
            case TypeID::UInt64: return "uint64";
            case TypeID::Float: return "float";
            case TypeID::Double: return "double";
            case TypeID::Bool: return "bool";
            case TypeID::Vec2: return "vec2";
            case TypeID::Vec3: return "vec3";
            case TypeID::Vec4: return "vec4";
            case TypeID::IVec2: return "ivec2";
            case TypeID::IVec3: return "ivec3";
            case TypeID::IVec4: return "ivec4";
            case TypeID::UVec2: return "uvec2";
            case TypeID::UVec3: return "uvec3";
            case TypeID::UVec4: return "uvec4";
            case TypeID::Mat2: return "mat2";
            case TypeID::Mat3: return "mat3";
            case TypeID::Mat4: return "mat4";
            case TypeID::Quat: return "quat";
            case TypeID::ColorRGB: return "color_rgb";
            case TypeID::ColorRGBA: return "color_rgba";
            case TypeID::String: return "string";
            case TypeID::CString: return "cstring";
            case TypeID::Vector: return "vector";
            case TypeID::Map: return "map";
            case TypeID::Struct: return "struct";
            case TypeID::Enum: return "enum";
            case TypeID::Asset: return "asset";
            case TypeID::Tag: return "tag";
            case TypeID::TagContainer: return "tag_container";
            case TypeID::TagQuery: return "tag_query";
            default: return "unknown";
        }
    }

    nlohmann::ordered_json DescribeField(const FieldInfo& field)
    {
        nlohmann::ordered_json out;
        out["name"] = field.name ? field.name : "";
        out["type"] = TypeName(field.type);
        out["editable"] = (field.flags & Editable) != 0;
        out["readOnly"] = (field.flags & ReadOnly) != 0;
        if (field.min != 0.0f || field.max != 0.0f)
        {
            out["min"] = field.min;
            out["max"] = field.max;
        }
        if (field.tooltip && field.tooltip[0])
            out["tooltip"] = field.tooltip;
        if (field.container)
            out["element"] = TypeName(field.container->elementType);
        if (field.enumDesc)
        {
            out["enum"] = field.enumDesc->name;
            nlohmann::ordered_json values = nlohmann::ordered_json::array();
            for (const EnumValueInfo& value : field.enumDesc->values)
                values.push_back({ { "name", value.name }, { "value", value.value } });
            out["values"] = values;
        }
        return out;
    }

    nlohmann::ordered_json DescribeClass(const ClassDescriptor& descriptor)
    {
        nlohmann::ordered_json out;
        out["name"] = descriptor.name;
        nlohmann::ordered_json fields = nlohmann::ordered_json::array();
        for (const FieldInfo* field : descriptor.fields)
            if (field)
                fields.push_back(DescribeField(*field));
        out["fields"] = fields;
        return out;
    }

    const FieldInfo* FindField(const ClassDescriptor& descriptor, const std::string& name)
    {
        for (const FieldInfo* field : descriptor.fields)
            if (field && field->name && name == field->name)
                return field;
        return nullptr;
    }

    bool GetFieldValue(const ClassDescriptor& descriptor, const void* object, const std::string& name, nlohmann::ordered_json& out, const ReflectionContext& context)
    {
        FieldInfo* field = const_cast<FieldInfo*>(FindField(descriptor, name));
        if (!field || !object)
            return false;
        nlohmann::ordered_json all;
        WriteFields({ field }, object, all, context);
        if (!all.contains(name))
            return false;
        out = all[name];
        return true;
    }

    bool SetFieldValue(const ClassDescriptor& descriptor, void* object, const std::string& name, const nlohmann::json& value, const ReflectionContext& context)
    {
        FieldInfo* field = const_cast<FieldInfo*>(FindField(descriptor, name));
        if (!field || !object || (field->flags & ReadOnly))
            return false;
        nlohmann::json in;
        in[name] = value;
        ReadFields({ field }, object, in, context);
        return true;
    }
}

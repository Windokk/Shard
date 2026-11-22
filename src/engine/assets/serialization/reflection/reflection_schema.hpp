#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/assets/serialization/reflection/reflection_serializer.hpp"

namespace Shard::Engine::Serialization
{
    const char* TypeName(TypeID type);

    nlohmann::ordered_json DescribeField(const FieldInfo& field);

    nlohmann::ordered_json DescribeClass(const ClassDescriptor& descriptor);

    const FieldInfo* FindField(const ClassDescriptor& descriptor, const std::string& name);

    bool GetFieldValue(const ClassDescriptor& descriptor, const void* object, const std::string& name, nlohmann::ordered_json& out, const ReflectionContext& context);

    bool SetFieldValue(const ClassDescriptor& descriptor, void* object, const std::string& name, const nlohmann::json& value, const ReflectionContext& context);
}

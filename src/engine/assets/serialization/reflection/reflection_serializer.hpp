#pragma once

#include <vector>
#include <functional>

#include <nlohmann/json.hpp>

#include "engine/assets/assetID.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

namespace Shard::Engine::Serialization
{
    struct ReflectionContext
    {
        Filesystem::AssetIDManager* assets = nullptr;
    };

    void WriteFields(const std::vector<FieldInfo*>& fields, const void* object, nlohmann::ordered_json& out, const ReflectionContext& context);

    void ReadFields(const std::vector<FieldInfo*>& fields, void* object, const nlohmann::json& in, const ReflectionContext& context,
                    const std::function<void(const FieldInfo&)>& onChanged = {});

    bool WriteValue(const FieldInfo& field, TypeID type, const void* value, nlohmann::ordered_json& out, const ReflectionContext& context);

    bool ReadValue(const FieldInfo& field, TypeID type, void* value, const nlohmann::json& in, const ReflectionContext& context);
}

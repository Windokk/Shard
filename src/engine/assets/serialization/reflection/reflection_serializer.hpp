#pragma once

#include <vector>
#include <functional>

#include <nlohmann/json.hpp>

#include "engine/assets/assetID.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

namespace Shard::Engine::Serialization
{
    /// What the reflected values need to be (de)serialized : assets are stored by their name in the project, not by their runtime ID
    struct ReflectionContext
    {
        Filesystem::AssetIDManager* assets = nullptr;
    };

    /// @brief Writes the fields of an object into `out`, one key per field (its name). The json mirrors what the editor shows :
    /// vectors as {x,y,z}, colors as {r,g,b}, rotations as euler angles in degrees, enums by name, assets by name in the project.
    /// ReadOnly fields (runtime state the editor only shows) and fields whose type has no json form (structs) are skipped.
    /// @param object The object the fields' offsets are relative to
    void WriteFields(const std::vector<FieldInfo*>& fields, const void* object, nlohmann::ordered_json& out, const ReflectionContext& context);

    /// @brief Reads the fields of an object from `in`. A missing or ill-typed key leaves the field untouched (its default),
    /// ReadOnly fields are never read.
    /// @param onChanged Called for each field that was read (the same hook the editor calls once it edited a field), may be empty
    void ReadFields(const std::vector<FieldInfo*>& fields, void* object, const nlohmann::json& in, const ReflectionContext& context,
                    const std::function<void(const FieldInfo&)>& onChanged = {});

    /// @brief Writes one value of the given type (the building block of WriteFields)
    /// @return false if the type has no json form
    bool WriteValue(const FieldInfo& field, TypeID type, const void* value, nlohmann::ordered_json& out, const ReflectionContext& context);

    /// @brief Reads one value of the given type (the building block of ReadFields)
    /// @return false if `in` doesn't hold a value of this type (`value` is untouched then)
    bool ReadValue(const FieldInfo& field, TypeID type, void* value, const nlohmann::json& in, const ReflectionContext& context);
}

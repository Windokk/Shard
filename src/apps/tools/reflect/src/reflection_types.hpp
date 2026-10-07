#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <iostream>

enum class TypeID : uint16_t {
    // Invalid / base
    Unknown = 0,

    // Integer types
    Int8,
    Int16,
    Int32,
    Int64,

    UInt8,
    UInt16,
    UInt32,
    UInt64,

    // Floating point
    Float,
    Double,

    // Boolean
    Bool,

    // Math vectors
    // (glm / custom)
    Vec2,
    Vec3,
    Vec4,

    IVec2,
    IVec3,
    IVec4,

    UVec2,
    UVec3,
    UVec4,

    // Matrices
    Mat2,
    Mat3,
    Mat4,

    // Quaternion
    Quat,

    // Colors
    ColorRGB,
    ColorRGBA,

    // Strings
    String,        // std::string
    CString,       // char*

    // Containers
    Vector,
    Map,

    // User-defined / fallback
    Struct,
    Enum,

    Asset
};

struct EnumValueInfo {
    const char* name;
    int value; // underlying integer value
};

struct EnumDescriptor {
    std::string name;
    std::vector<EnumValueInfo> values;
};

static TypeID GetTypeIDFromString(std::string typeName) {
    // Remove const/reference/pointer noise
    auto normalize = [](std::string& s) {
        s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
        if (s.compare(0, 4, "const") == 0)
            s = s.substr(5);
        if (!s.empty() && (s.back() == '&' || s.back() == '*'))
            s.pop_back();
    };

    normalize(typeName);

    // Template detection
    if (typeName.compare(0, 12, "std::vector<") == 0)
        return TypeID::Vector;

    if (typeName.compare(0, 8, "std::map<") == 0)
        return TypeID::Map;

    static const std::unordered_map<std::string, TypeID> typeMap = {
        // Integer types
        {"int8_t", TypeID::Int8},
        {"int16_t", TypeID::Int16},
        {"int32_t", TypeID::Int32},
        {"int64_t", TypeID::Int64},

        {"uint8_t", TypeID::UInt8},
        {"uint16_t", TypeID::UInt16},
        {"uint32_t", TypeID::UInt32},
        {"uint64_t", TypeID::UInt64},

        // Floating point
        {"float", TypeID::Float},
        {"double", TypeID::Double},

        // Boolean
        {"bool", TypeID::Bool},

        // Math vectors (glm)
        {"glm::vec2", TypeID::Vec2},
        {"glm::vec3", TypeID::Vec3},
        {"glm::vec4", TypeID::Vec4},

        {"glm::ivec2", TypeID::IVec2},
        {"glm::ivec3", TypeID::IVec3},
        {"glm::ivec4", TypeID::IVec4},

        {"glm::uvec2", TypeID::UVec2},
        {"glm::uvec3", TypeID::UVec3},
        {"glm::uvec4", TypeID::UVec4},

        // Matrices
        {"glm::mat2", TypeID::Mat2},
        {"glm::mat3", TypeID::Mat3},
        {"glm::mat4", TypeID::Mat4},

        // Quaternion
        {"glm::quat", TypeID::Quat},

        // Colors
        {"COL_RGB", TypeID::ColorRGB},
        {"COL_RGBA", TypeID::ColorRGBA},

        // Strings
        {"std::string", TypeID::String},
        {"char*", TypeID::CString},

        // User-defined
        {"struct", TypeID::Struct},
        {"enum", TypeID::Enum},

        
        {"Shard::Engine::Filesystem::AssetID", TypeID::Asset},
        {"Engine::Filesystem::AssetID", TypeID::Asset},
        {"Filesystem::AssetID", TypeID::Asset},
        {"AssetID", TypeID::Asset}
    };

    if (auto it = typeMap.find(typeName); it != typeMap.end())
        return it->second;

    return TypeID::Unknown; // fallback if not found
}

static std::string GetStringFromTypeID(TypeID type) {
    switch (type) {
        // Invalid / base
        case TypeID::Unknown: return "Unknown";

        // Integer types
        case TypeID::Int8:  return "Int8";
        case TypeID::Int16: return "Int16";
        case TypeID::Int32: return "Int32";
        case TypeID::Int64: return "Int64";

        case TypeID::UInt8:  return "UInt8";
        case TypeID::UInt16: return "UInt16";
        case TypeID::UInt32: return "UInt32";
        case TypeID::UInt64: return "UInt64";

        // Floating point
        case TypeID::Float:  return "Float";
        case TypeID::Double: return "Double";

        // Boolean
        case TypeID::Bool: return "Bool";

        // Vectors
        case TypeID::Vec2: return "Vec2";
        case TypeID::Vec3: return "Vec3";
        case TypeID::Vec4: return "Vec4";

        case TypeID::IVec2: return "IVec2";
        case TypeID::IVec3: return "IVec3";
        case TypeID::IVec4: return "IVec4";

        case TypeID::UVec2: return "UVec2";
        case TypeID::UVec3: return "UVec3";
        case TypeID::UVec4: return "UVec4";

        // Matrices
        case TypeID::Mat2: return "Mat2";
        case TypeID::Mat3: return "Mat3";
        case TypeID::Mat4: return "Mat4";

        // Quaternion
        case TypeID::Quat: return "Quat";

        // Colors
        case TypeID::ColorRGB: return "ColorRGB";
        case TypeID::ColorRGBA: return "ColorRGBA";

        // Strings
        case TypeID::String: return "String";
        case TypeID::CString: return "CString";

        // Containers
        case TypeID::Vector: return "Vector";
        case TypeID::Map: return "Map";

        // User-defined / fallback
        case TypeID::Struct: return "Struct";
        case TypeID::Enum: return "Enum";

        case TypeID::Asset: return "Asset";
    }

    return "Unknown"; // fallback, should never hit
}

inline size_t GetTypeSize(TypeID type) {
    switch (type) {
        case TypeID::Int8:   return sizeof(int8_t);
        case TypeID::Int16:  return sizeof(int16_t);
        case TypeID::Int32:  return sizeof(int32_t);
        case TypeID::Int64:  return sizeof(int64_t);
        case TypeID::UInt8:  return sizeof(uint8_t);
        case TypeID::UInt16: return sizeof(uint16_t);
        case TypeID::UInt32: return sizeof(uint32_t);
        case TypeID::UInt64: return sizeof(uint64_t);
        case TypeID::Float:  return sizeof(float);
        case TypeID::Double: return sizeof(double);
        case TypeID::Bool:   return sizeof(bool);

        case TypeID::Vec2: return sizeof(float) * 2;
        case TypeID::Vec3: return sizeof(float) * 3;
        case TypeID::Vec4: return sizeof(float) * 4;
        case TypeID::IVec2: return sizeof(int) * 2;
        case TypeID::IVec3: return sizeof(int) * 3;
        case TypeID::IVec4: return sizeof(int) * 4;
        case TypeID::UVec2: return sizeof(uint64_t) * 2;
        case TypeID::UVec3: return sizeof(uint64_t) * 3;
        case TypeID::UVec4: return sizeof(uint64_t) * 4;

        case TypeID::Mat2: return sizeof(float) * 4;
        case TypeID::Mat3: return sizeof(float) * 9;
        case TypeID::Mat4: return sizeof(float) * 16;

        case TypeID::Quat: return sizeof(float) * 4;

        case TypeID::ColorRGB: return sizeof(float) * 3;
        case TypeID::ColorRGBA: return sizeof(float) * 4;

        case TypeID::String: return sizeof(std::string);
        case TypeID::CString: return sizeof(char*);

        case TypeID::Asset: return sizeof(int);

        default:
            return 0; // containers / structs handled separately
    }
}
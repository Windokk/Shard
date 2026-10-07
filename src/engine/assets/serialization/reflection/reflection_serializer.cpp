#include "reflection_serializer.hpp"

#include <cstring>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Serialization
{
    using nlohmann::json;
    using nlohmann::ordered_json;

    namespace
    {
        // ---- Vectors / colors : N scalars of the same type, laid out contiguously, named by axis ----

        template<typename T, size_t N>
        void WriteComponents(const void* value, ordered_json& out, const char* const (&keys)[N])
        {
            const T* components = static_cast<const T*>(value);
            for (size_t i = 0; i < N; ++i)
                out[keys[i]] = components[i];
        }

        /// A missing component keeps its current value, only a non-object is an error
        template<typename T, size_t N>
        bool ReadComponents(void* value, const json& in, const char* const (&keys)[N])
        {
            if (!in.is_object())
                return false;

            T* components = static_cast<T*>(value);
            for (size_t i = 0; i < N; ++i)
            {
                if (in.contains(keys[i]) && in[keys[i]].is_number())
                    components[i] = in[keys[i]].template get<T>();
            }
            return true;
        }

        constexpr const char* kXY[]   = { "x", "y" };
        constexpr const char* kXYZ[]  = { "x", "y", "z" };
        constexpr const char* kXYZW[] = { "x", "y", "z", "w" };
        constexpr const char* kRGB[]  = { "r", "g", "b" };
        constexpr const char* kRGBA[] = { "r", "g", "b", "a" };

        // ---- Matrices : a flat array, column by column ----

        template<size_t N>
        void WriteMatrix(const void* value, ordered_json& out)
        {
            const float* m = static_cast<const float*>(value);
            out = ordered_json::array();
            for (size_t i = 0; i < N * N; ++i)
                out.push_back(m[i]);
        }

        template<size_t N>
        bool ReadMatrix(void* value, const json& in)
        {
            if (!in.is_array() || in.size() != N * N)
                return false;

            float* m = static_cast<float*>(value);
            for (size_t i = 0; i < N * N; ++i)
            {
                if (!in[i].is_number())
                    return false;
                m[i] = in[i].get<float>();
            }
            return true;
        }

        template<typename T>
        bool ReadNumber(void* value, const json& in)
        {
            if (!in.is_number())
                return false;
            *static_cast<T*>(value) = in.get<T>();
            return true;
        }

        template<typename T>
        void WriteNumber(const void* value, ordered_json& out)
        {
            out = *static_cast<const T*>(value);
        }

        int ReadEnumValue(const void* value, size_t size)
        {
            int v = 0;
            std::memcpy(&v, value, size < sizeof(int) ? size : sizeof(int));
            return v;
        }

        /// Reads one element of a container into a scratch buffer and appends it
        bool AppendElement(const FieldInfo& field, void* container, const json& in, const ReflectionContext& context)
        {
            const Container& c = *field.container;

            // std::string needs a constructed object, everything else the editor can show is plain data
            if (c.elementType == TypeID::String)
            {
                std::string element;
                if (!ReadValue(field, TypeID::String, &element, in, context))
                    return false;
                c.InsertAt(container, c.Size(container), &element);
                return true;
            }

            alignas(16) unsigned char element[128] = {};
            if (c.elementSize > sizeof(element))
                return false;

            if (!ReadValue(field, c.elementType, element, in, context))
                return false;

            c.InsertAt(container, c.Size(container), element);
            return true;
        }
    }

    bool WriteValue(const FieldInfo& field, TypeID type, const void* value, ordered_json& out, const ReflectionContext& context)
    {
        switch (type)
        {
            case TypeID::Int8:   WriteNumber<int8_t>(value, out);   return true;
            case TypeID::Int16:  WriteNumber<int16_t>(value, out);  return true;
            case TypeID::Int32:  WriteNumber<int32_t>(value, out);  return true;
            case TypeID::Int64:  WriteNumber<int64_t>(value, out);  return true;
            case TypeID::UInt8:  WriteNumber<uint8_t>(value, out);  return true;
            case TypeID::UInt16: WriteNumber<uint16_t>(value, out); return true;
            case TypeID::UInt32: WriteNumber<uint32_t>(value, out); return true;
            case TypeID::UInt64: WriteNumber<uint64_t>(value, out); return true;
            case TypeID::Float:  WriteNumber<float>(value, out);    return true;
            case TypeID::Double: WriteNumber<double>(value, out);   return true;
            case TypeID::Bool:   WriteNumber<bool>(value, out);     return true;

            case TypeID::Vec2:  WriteComponents<float>(value, out, kXY);   return true;
            case TypeID::Vec3:  WriteComponents<float>(value, out, kXYZ);  return true;
            case TypeID::Vec4:  WriteComponents<float>(value, out, kXYZW); return true;
            case TypeID::IVec2: WriteComponents<int>(value, out, kXY);     return true;
            case TypeID::IVec3: WriteComponents<int>(value, out, kXYZ);    return true;
            case TypeID::IVec4: WriteComponents<int>(value, out, kXYZW);   return true;
            case TypeID::UVec2: WriteComponents<unsigned>(value, out, kXY);   return true;
            case TypeID::UVec3: WriteComponents<unsigned>(value, out, kXYZ);  return true;
            case TypeID::UVec4: WriteComponents<unsigned>(value, out, kXYZW); return true;

            case TypeID::ColorRGB:  WriteComponents<float>(value, out, kRGB);  return true;
            case TypeID::ColorRGBA: WriteComponents<float>(value, out, kRGBA); return true;

            case TypeID::Mat2: WriteMatrix<2>(value, out); return true;
            case TypeID::Mat3: WriteMatrix<3>(value, out); return true;
            case TypeID::Mat4: WriteMatrix<4>(value, out); return true;

            case TypeID::Quat:
            {
                // Euler angles in degrees : what the editor shows and what the levels always stored for a rotation
                const glm::vec3 euler = glm::degrees(glm::eulerAngles(*static_cast<const glm::quat*>(value)));
                out["x"] = euler.x;
                out["y"] = euler.y;
                out["z"] = euler.z;
                return true;
            }

            case TypeID::String:
                out = *static_cast<const std::string*>(value);
                return true;

            case TypeID::CString:
            {
                const char* str = *static_cast<const char* const*>(value);
                out = str ? str : "";
                return true;
            }

            case TypeID::Enum:
            {
                if (!field.enumDesc)
                    return false;

                const int v = ReadEnumValue(value, field.enumDesc->size);
                for (const EnumValueInfo& e : field.enumDesc->values)
                {
                    if (e.value == v)
                    {
                        out = e.name;
                        return true;
                    }
                }
                out = v; // not a named value, keep the number so that nothing is lost
                return true;
            }

            case TypeID::Asset:
            {
                const Filesystem::AssetID id = *static_cast<const Filesystem::AssetID*>(value);

                std::shared_ptr<Filesystem::AssetInfos> asset = context.assets ? context.assets->GetAssetFromID(id) : nullptr;
                out = asset ? asset->baseInfos.nameInProject : "";
                return true;
            }

            case TypeID::Vector:
            {
                if (!field.container || field.container->IsAssociative())
                    return false;

                void* container = const_cast<void*>(value);
                const size_t count = field.container->Size(container);

                out = ordered_json::array();
                for (size_t i = 0; i < count; ++i)
                {
                    ordered_json element;
                    if (!WriteValue(field, field.container->elementType, field.container->GetByIndex(container, i), element, context))
                        return false;
                    out.push_back(std::move(element));
                }
                return true;
            }

            default:
                return false; // Struct, Map : no json form yet
        }
    }

    bool ReadValue(const FieldInfo& field, TypeID type, void* value, const json& in, const ReflectionContext& context)
    {
        switch (type)
        {
            case TypeID::Int8:   return ReadNumber<int8_t>(value, in);
            case TypeID::Int16:  return ReadNumber<int16_t>(value, in);
            case TypeID::Int32:  return ReadNumber<int32_t>(value, in);
            case TypeID::Int64:  return ReadNumber<int64_t>(value, in);
            case TypeID::UInt8:  return ReadNumber<uint8_t>(value, in);
            case TypeID::UInt16: return ReadNumber<uint16_t>(value, in);
            case TypeID::UInt32: return ReadNumber<uint32_t>(value, in);
            case TypeID::UInt64: return ReadNumber<uint64_t>(value, in);
            case TypeID::Float:  return ReadNumber<float>(value, in);
            case TypeID::Double: return ReadNumber<double>(value, in);

            case TypeID::Bool:
                if (!in.is_boolean())
                    return false;
                *static_cast<bool*>(value) = in.get<bool>();
                return true;

            case TypeID::Vec2:  return ReadComponents<float>(value, in, kXY);
            case TypeID::Vec3:  return ReadComponents<float>(value, in, kXYZ);
            case TypeID::Vec4:  return ReadComponents<float>(value, in, kXYZW);
            case TypeID::IVec2: return ReadComponents<int>(value, in, kXY);
            case TypeID::IVec3: return ReadComponents<int>(value, in, kXYZ);
            case TypeID::IVec4: return ReadComponents<int>(value, in, kXYZW);
            case TypeID::UVec2: return ReadComponents<unsigned>(value, in, kXY);
            case TypeID::UVec3: return ReadComponents<unsigned>(value, in, kXYZ);
            case TypeID::UVec4: return ReadComponents<unsigned>(value, in, kXYZW);

            case TypeID::ColorRGB:  return ReadComponents<float>(value, in, kRGB);
            case TypeID::ColorRGBA: return ReadComponents<float>(value, in, kRGBA);

            case TypeID::Mat2: return ReadMatrix<2>(value, in);
            case TypeID::Mat3: return ReadMatrix<3>(value, in);
            case TypeID::Mat4: return ReadMatrix<4>(value, in);

            case TypeID::Quat:
            {
                if (!in.is_object())
                    return false;

                glm::quat& q = *static_cast<glm::quat*>(value);

                if (in.contains("w"))
                {
                    // A raw quaternion
                    float c[4] = { q.x, q.y, q.z, q.w };
                    if (!ReadComponents<float>(c, in, kXYZW))
                        return false;
                    q = glm::quat(c[3], c[0], c[1], c[2]);
                }
                else
                {
                    glm::vec3 euler = glm::degrees(glm::eulerAngles(q));
                    if (!ReadComponents<float>(&euler, in, kXYZ))
                        return false;
                    q = glm::quat(glm::radians(euler));
                }
                return true;
            }

            case TypeID::String:
                if (!in.is_string())
                    return false;
                *static_cast<std::string*>(value) = in.get<std::string>();
                return true;

            case TypeID::Enum:
            {
                if (!field.enumDesc)
                    return false;

                int v = 0;
                if (in.is_string())
                {
                    const std::string name = in.get<std::string>();
                    bool found = false;
                    for (const EnumValueInfo& e : field.enumDesc->values)
                    {
                        if (name == e.name)
                        {
                            v = e.value;
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        DEBUG_WARNING("Unknown value '" + name + "' for enum " + field.enumDesc->name);
                        return false;
                    }
                }
                else if (in.is_number_integer())
                    v = in.get<int>();
                else
                    return false;

                std::memcpy(value, &v, field.enumDesc->size < sizeof(int) ? field.enumDesc->size : sizeof(int));
                return true;
            }

            case TypeID::Asset:
            {
                if (in.is_number_integer())
                {
                    *static_cast<Filesystem::AssetID*>(value) = Filesystem::AssetID(in.get<int>());
                    return true;
                }

                if (!in.is_string() || !context.assets)
                    return false;

                const std::string name = in.get<std::string>();
                *static_cast<Filesystem::AssetID*>(value) = name.empty() ? Filesystem::AssetID() : context.assets->GetIDFromNameInProject(name);
                return true;
            }

            case TypeID::Vector:
            {
                if (!in.is_array() || !field.container || field.container->IsAssociative() || !field.container->InsertAt)
                    return false;

                // The vector is rebuilt from the file : what it held (the default) is dropped
                if (field.container->Clear)
                    field.container->Clear(value);

                for (const json& element : in)
                {
                    if (!AppendElement(field, value, element, context))
                        DEBUG_WARNING(std::string("Skipped an invalid element of '") + field.name + "'");
                }
                return true;
            }

            default:
                return false;
        }
    }

    void WriteFields(const std::vector<FieldInfo*>& fields, const void* object, ordered_json& out, const ReflectionContext& context)
    {
        for (const FieldInfo* field : fields)
        {
            if (field->flags & ReadOnly)
                continue;

            ordered_json value;
            if (WriteValue(*field, field->type, static_cast<const uint8_t*>(object) + field->offset, value, context))
                out[field->name] = std::move(value);
        }
    }

    void ReadFields(const std::vector<FieldInfo*>& fields, void* object, const json& in, const ReflectionContext& context,
                    const std::function<void(const FieldInfo&)>& onChanged)
    {
        if (!in.is_object())
            return;

        for (const FieldInfo* field : fields)
        {
            if (field->flags & ReadOnly)
                continue;

            auto it = in.find(field->name);
            if (it == in.end())
                continue;

            if (!ReadValue(*field, field->type, static_cast<uint8_t*>(object) + field->offset, *it, context))
            {
                DEBUG_WARNING(std::string("Invalid value for field '") + field->name + "', keeping its default");
                continue;
            }

            if (onChanged)
                onChanged(*field);
        }
    }
}

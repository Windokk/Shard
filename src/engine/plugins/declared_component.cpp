#include "engine/plugins/declared_component.hpp"

#include <algorithm>
#include <cstddef>
#include <set>

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Plugins {

    using nlohmann::json;
    using Objects::Components::Component;

    namespace {
        using Float3 = std::array<float, 3>;
        using Float4 = std::array<float, 4>;

        struct Layout {
            TypeID type;
            size_t size;
            size_t align;
        };

        bool LayoutOf(const std::string& declaredType, Layout& out)
        {
            if (declaredType == "bool") out = { TypeID::Bool, sizeof(bool), alignof(bool) };
            else if (declaredType == "int") out = { TypeID::Int32, sizeof(int32_t), alignof(int32_t) };
            else if (declaredType == "float") out = { TypeID::Float, sizeof(float), alignof(float) };
            else if (declaredType == "string") out = { TypeID::String, sizeof(std::string), alignof(std::string) };
            else if (declaredType == "vec3") out = { TypeID::Vec3, sizeof(Float3), alignof(Float3) };
            else if (declaredType == "color") out = { TypeID::ColorRGBA, sizeof(Float4), alignof(Float4) };
            else return false;
            return true;
        }

        template <typename T>
        void Fill(FieldInfo& f)
        {
            f.CopyConstruct = &CopyConstruct<T>;
            f.Assign = &Assign<T>;
            f.Destroy = &Destroy<T>;
            f.Equals = &Equals<T>;
        }

        void FillFor(FieldInfo& f)
        {
            switch (f.type) {
                case TypeID::Bool: Fill<bool>(f); break;
                case TypeID::Int32: Fill<int32_t>(f); break;
                case TypeID::Float: Fill<float>(f); break;
                case TypeID::String: Fill<std::string>(f); break;
                case TypeID::Vec3: Fill<Float3>(f); break;
                case TypeID::ColorRGBA: Fill<Float4>(f); break;
                default: break;
            }
        }

        // Writes the declared default into storage at `at`
        void ConstructValue(const FieldInfo& f, unsigned char* at, const json& value)
        {
            switch (f.type) {
                case TypeID::Bool: new (at) bool(value.is_boolean() ? value.get<bool>() : false); break;
                case TypeID::Int32: new (at) int32_t(value.is_number() ? value.get<int32_t>() : 0); break;
                case TypeID::Float: new (at) float(value.is_number() ? value.get<float>() : 0.0f); break;
                case TypeID::String: new (at) std::string(value.is_string() ? value.get<std::string>() : std::string()); break;
                case TypeID::Vec3: {
                    Float3 v{ 0.f, 0.f, 0.f };
                    if (value.is_array() && value.size() == 3) for (size_t i = 0; i < 3; ++i) v[i] = value[i].is_number() ? value[i].get<float>() : 0.f;
                    new (at) Float3(v);
                    break;
                }
                case TypeID::ColorRGBA: {
                    Float4 v{ 1.f, 1.f, 1.f, 1.f };
                    if (value.is_array() && (value.size() == 3 || value.size() == 4)) for (size_t i = 0; i < value.size(); ++i) v[i] = value[i].is_number() ? value[i].get<float>() : 1.f;
                    new (at) Float4(v);
                    break;
                }
                default: break;
            }
        }

        bool IsIdentifier(const std::string& s)
        {
            if (s.empty() || s.size() > 64) return false;
            if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
            return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
        }
    }

    size_t DeclaredComponentType::StorageOffset()
    {
#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
        return offsetof(DeclaredFieldsComponent, m_Storage);
#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic pop
#endif
    }

    Core::Result<std::shared_ptr<DeclaredComponentType>> DeclaredComponentType::Build(const DeclaredComponent& declaration)
    {
        if (declaration.name.empty()) return Core::Err("a component needs a name");
        if (declaration.fields.size() > kMaxFields) return Core::Err("'" + declaration.name + "' has more than " + std::to_string(kMaxFields) + " fields");

        auto type = std::shared_ptr<DeclaredComponentType>(new DeclaredComponentType());
        type->m_Descriptor.name = declaration.name;
        type->m_Declared = declaration.fields;
        type->m_StorageBase = StorageOffset();

        std::set<std::string> seen;
        size_t cursor = 0;
        for (const DeclaredField& declared : declaration.fields) {
            if (!IsIdentifier(declared.name)) return Core::Err("'" + declaration.name + "' : '" + declared.name + "' is not a field name (letters, digits and '_', not starting with a digit)");
            if (declared.name == "type" || declared.name == "active") return Core::Err("'" + declaration.name + "' : the field name '" + declared.name + "' is reserved");
            if (!seen.insert(declared.name).second) return Core::Err("'" + declaration.name + "' : the field '" + declared.name + "' is declared twice");
            Layout layout;
            if (!LayoutOf(declared.type, layout)) return Core::Err("'" + declaration.name + "' : unknown field type '" + declared.type + "'");

            cursor = (cursor + layout.align - 1) / layout.align * layout.align;
            if (cursor + layout.size > kMaxStorage) return Core::Err("'" + declaration.name + "' holds too much (" + std::to_string(kMaxStorage) + " bytes of fields at most)");

            type->m_Names.push_back(declared.name);
            FieldInfo info{};
            info.name = type->m_Names.back().c_str();
            info.type = layout.type;
            info.offset = uint32_t(type->m_StorageBase + cursor);
            info.flags = Editable;
            info.min = 0.0f;
            info.max = 0.0f;
            info.tooltip = nullptr;
            FillFor(info);
            type->m_FieldInfos.push_back(info);
            cursor += layout.size;
        }
        type->m_Size = cursor;
        for (FieldInfo& f : type->m_FieldInfos) type->m_Descriptor.fields.push_back(&f);

        // The defaults, constructed once in a buffer that has the layout of the storage
        type->m_Defaults.assign(type->m_StorageBase + std::max<size_t>(type->m_Size, 1), 0);
        for (size_t i = 0; i < type->m_FieldInfos.size(); ++i)
            ConstructValue(type->m_FieldInfos[i], type->m_Defaults.data() + type->m_FieldInfos[i].offset, declaration.fields[i].defaultValue);
        return Core::Ok(std::move(type));
    }

    DeclaredComponentType::~DeclaredComponentType()
    {
        // The defaults were constructed in a buffer of our own
        if (!m_Defaults.empty())
            for (const FieldInfo& f : m_FieldInfos) f.Destroy(m_Defaults.data() + f.offset);
    }

    const FieldInfo* DeclaredComponentType::FindField(const std::string& name) const
    {
        for (const FieldInfo& f : m_FieldInfos) if (name == f.name) return &f;
        return nullptr;
    }

    void DeclaredComponentType::ConstructDefaults(unsigned char* storage) const
    {
        // `storage` is the start of the component's field storage ; FieldInfo offsets count from the component itself
        unsigned char* base = storage - m_StorageBase;
        for (const FieldInfo& f : m_FieldInfos) f.CopyConstruct(base + f.offset, m_Defaults.data() + f.offset);
    }

    void DeclaredComponentType::Destroy(unsigned char* storage) const
    {
        unsigned char* base = storage - m_StorageBase;
        for (const FieldInfo& f : m_FieldInfos) f.Destroy(base + f.offset);
    }

    void DeclaredComponentType::Assign(unsigned char* to, const unsigned char* from) const
    {
        unsigned char* tb = to - m_StorageBase;
        const unsigned char* fb = from - m_StorageBase;
        for (const FieldInfo& f : m_FieldInfos) f.Assign(tb + f.offset, fb + f.offset);
    }

    // ---- The component ----

    DeclaredFieldsComponent::DeclaredFieldsComponent(std::shared_ptr<Objects::Actor> parent, uint32_t localId, std::shared_ptr<const DeclaredComponentType> type)
        : Component(std::move(parent), localId), m_Type(std::move(type))
    {
        // FieldInfo offsets count from the Component sub-object, which has to be where this object starts
        const auto here = reinterpret_cast<const unsigned char*>(static_cast<const Component*>(this));
        const auto storage = reinterpret_cast<const unsigned char*>(m_Storage);
        if (size_t(storage - here) != DeclaredComponentType::StorageOffset())
            DEBUG_ERROR("Declared component '", m_Type->Name(), "' : the storage is not where its fields expect it");
        m_Type->ConstructDefaults(m_Storage);
    }

    DeclaredFieldsComponent::~DeclaredFieldsComponent()
    {
        m_Type->Destroy(m_Storage);
    }

    std::shared_ptr<Component> DeclaredFieldsComponent::Clone() const
    {
        auto copy = Core::Object::Create<DeclaredFieldsComponent>(nullptr, 0, m_Type);
        m_Type->Assign(copy->m_Storage, m_Storage);
        copy->activated = activated;
        return copy;
    }

    json DeclaredFieldsComponent::Get(const std::string& field) const
    {
        const FieldInfo* f = m_Type->FindField(field);
        if (!f) return json();
        const unsigned char* at = reinterpret_cast<const unsigned char*>(this) + f->offset;
        switch (f->type) {
            case TypeID::Bool: return *reinterpret_cast<const bool*>(at);
            case TypeID::Int32: return *reinterpret_cast<const int32_t*>(at);
            case TypeID::Float: return *reinterpret_cast<const float*>(at);
            case TypeID::String: return *reinterpret_cast<const std::string*>(at);
            case TypeID::Vec3: { const auto& v = *reinterpret_cast<const Float3*>(at); return json::array({ v[0], v[1], v[2] }); }
            case TypeID::ColorRGBA: { const auto& v = *reinterpret_cast<const Float4*>(at); return json::array({ v[0], v[1], v[2], v[3] }); }
            default: return json();
        }
    }

    bool DeclaredFieldsComponent::Set(const std::string& field, const json& value)
    {
        const FieldInfo* f = m_Type->FindField(field);
        if (!f) return false;
        unsigned char* at = reinterpret_cast<unsigned char*>(this) + f->offset;
        switch (f->type) {
            case TypeID::Bool: if (!value.is_boolean()) return false; *reinterpret_cast<bool*>(at) = value.get<bool>(); return true;
            case TypeID::Int32: if (!value.is_number_integer()) return false; *reinterpret_cast<int32_t*>(at) = value.get<int32_t>(); return true;
            case TypeID::Float: if (!value.is_number()) return false; *reinterpret_cast<float*>(at) = value.get<float>(); return true;
            case TypeID::String: if (!value.is_string()) return false; *reinterpret_cast<std::string*>(at) = value.get<std::string>(); return true;
            case TypeID::Vec3:
            case TypeID::ColorRGBA: {
                const size_t n = f->type == TypeID::Vec3 ? 3 : 4;
                if (!value.is_array() || value.size() != n) return false;
                for (const json& c : value) if (!c.is_number()) return false;
                float* out = reinterpret_cast<float*>(at);
                for (size_t i = 0; i < n; ++i) out[i] = value[i].get<float>();
                return true;
            }
            default: return false;
        }
    }

    json DeclaredFieldsComponent::Values() const
    {
        json out = json::object();
        for (const FieldInfo* f : m_Type->Descriptor().fields) out[f->name] = Get(f->name);
        return out;
    }

    Objects::Components::ComponentFactory MakeDeclaredFactory(std::shared_ptr<const DeclaredComponentType> type)
    {
        return [type]() -> std::shared_ptr<Component> { return Core::Object::Create<DeclaredFieldsComponent>(nullptr, 0, type); };
    }
}

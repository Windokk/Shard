// Shard C++ SDK : component types from C++ structs.
//
//   struct Spin { float speed = 1.f; float angle = 0.f; Shard::Vec3 axis{ 0.f, 1.f, 0.f }; };
//   SHARD_COMPONENT(Spin, (speed)(angle)(axis))
//
// (at global scope) declares which members are fields (their type and offset are deduced) ; the struct's default member values are the
// defaults of a new component. The struct must be trivially copyable and standard layout : it is the block the engine copies with memcpy (shard_components.h).
// Register it in OnLoad with `ctx.Components().Register<Spin>("Spin")`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <vector>

#include <shard/shard_components.h>
#include <shard/cpp/detail.hpp>

namespace Shard {

    // Layout-compatible stand-ins for the vector types of the ABI (any type of the same layout can be mapped with FieldType below)
    struct Vec2 { float x = 0.f, y = 0.f; };
    struct Vec3 { float x = 0.f, y = 0.f, z = 0.f; };
    struct Vec4 { float x = 0.f, y = 0.f, z = 0.f, w = 0.f; };
    struct Quat { float x = 0.f, y = 0.f, z = 0.f, w = 1.f; };
    struct Color { float r = 1.f, g = 1.f, b = 1.f, a = 1.f; };

    /// The SHARD_FIELD_* of a member type. Specialize it for a type of your own with the same layout (glm::vec3 -> SHARD_FIELD_VEC3).
    template <typename T, typename = void> struct FieldType { static constexpr uint32_t value = 0; };
    template <> struct FieldType<float> { static constexpr uint32_t value = SHARD_FIELD_F32; };
    template <> struct FieldType<double> { static constexpr uint32_t value = SHARD_FIELD_F64; };
    template <> struct FieldType<int32_t> { static constexpr uint32_t value = SHARD_FIELD_I32; };
    template <> struct FieldType<uint32_t> { static constexpr uint32_t value = SHARD_FIELD_U32; };
    template <> struct FieldType<int64_t> { static constexpr uint32_t value = SHARD_FIELD_I64; };
    template <> struct FieldType<uint64_t> { static constexpr uint32_t value = SHARD_FIELD_U64; };
    template <> struct FieldType<bool> { static constexpr uint32_t value = SHARD_FIELD_BOOL; };
    template <> struct FieldType<Vec2> { static constexpr uint32_t value = SHARD_FIELD_VEC2; };
    template <> struct FieldType<Vec3> { static constexpr uint32_t value = SHARD_FIELD_VEC3; };
    template <> struct FieldType<Vec4> { static constexpr uint32_t value = SHARD_FIELD_VEC4; };
    template <> struct FieldType<Quat> { static constexpr uint32_t value = SHARD_FIELD_QUAT; };
    template <> struct FieldType<Color> { static constexpr uint32_t value = SHARD_FIELD_COLOR; };
    template <> struct FieldType<float[2]> { static constexpr uint32_t value = SHARD_FIELD_VEC2; };
    template <> struct FieldType<float[3]> { static constexpr uint32_t value = SHARD_FIELD_VEC3; };
    template <> struct FieldType<float[4]> { static constexpr uint32_t value = SHARD_FIELD_VEC4; };

    /// @brief A blob of this plugin (shard_components.h, 1.1) : bytes the host keeps outside a component's block, as many as needed. A component
    /// holds one in a member of this type ; Components::CreateBlob makes it. Copying the struct copies the handle, not the bytes.
    struct Blob {
        ShardBlob handle = 0;
        explicit operator bool() const { return handle != 0; }
    };
    template <> struct FieldType<Blob> { static constexpr uint32_t value = SHARD_FIELD_BLOB; };

    /// Flags a member type adds to its field : none, except for a std::string of a script, which the host reads through the script's callbacks
    template <typename T, typename = void> struct FieldFlags { static constexpr uint32_t value = 0; };
    /// A std::string member of a script is a field (shard/cpp/script.hpp) : the text is edited in the inspector and saved with the world. The
    /// string stays the plugin's own, the host never touches its memory. Not for a component, whose block is plain data.
    template <> struct FieldType<std::string> { static constexpr uint32_t value = SHARD_FIELD_STRING; };
    template <> struct FieldFlags<std::string> { static constexpr uint32_t value = SHARD_FIELD_ACCESSOR; };

    /// @brief A text of a component (shard_components.h, 1.2) : a field of this type holds a handle, the text lives in the host. The inspector
    /// shows it as a text box and the world file saves it as a string. Read it with Components::GetText, write it with Components::SetText.
    /// Copying the struct copies the handle, not the text.
    struct Text {
        ShardBlob handle = 0;

        /// The text (empty if there is none yet). thread : any
        std::string Get() const;
        /// Replaces the text. Throws Shard::Error over the memory budget, or in an engine older than ABI 1.3. thread : any
        void Set(std::string_view text);
        Text& operator=(std::string_view text) { Set(text); return *this; }
        Text& operator=(const char* text) { Set(text ? std::string_view(text) : std::string_view()); return *this; }
        Text& operator=(const std::string& text) { Set(text); return *this; }
        Text() = default;
        Text(const Text&) = default;
        Text& operator=(const Text&) = default;
    };
    template <> struct FieldType<Text> { static constexpr uint32_t value = SHARD_FIELD_STRING; };

    /// @brief The id of an asset of the project (shard_components.h, 1.2), 0 for none : the inspector shows an asset picker
    struct AssetId {
        int32_t id = 0;
        explicit operator bool() const { return id != 0; }
    };
    template <> struct FieldType<AssetId> { static constexpr uint32_t value = SHARD_FIELD_ASSET; };

    /// How a field shows in the editor and whether it is saved (see shard_components.h)
    struct FieldOptions {
        std::string name;                   ///< the member's name, as written in SHARD_COMPONENT
        uint32_t flags = 0;                 ///< SHARD_FIELD_RUNTIME, SHARD_FIELD_PRIVATE
        float min = 0.f, max = 0.f;
        std::string tooltip;

        /// Only the name is required : { "speed", 0, 0.f, 100.f }, { "frames", SHARD_FIELD_RUNTIME }
        FieldOptions(std::string fieldName, uint32_t fieldFlags = 0, float lowest = 0.f, float highest = 0.f, std::string hint = {})
            : name(std::move(fieldName)), flags(fieldFlags), min(lowest), max(highest), tooltip(std::move(hint)) {}
    };

    /// The fields of T, filled by the specialization SHARD_COMPONENT writes
    template <typename T>
    class FieldList {
    public:
        explicit FieldList(const T& sample) : m_Sample(sample) {}

        template <typename M>
        void Add(const char* name, M T::* member)
        {
            static_assert(FieldType<M>::value != 0, "this member type has no SHARD_FIELD_* : specialize Shard::FieldType for it");
            const char* base = reinterpret_cast<const char*>(&m_Sample);
            const char* at = reinterpret_cast<const char*>(&(m_Sample.*member));
            Entry e;
            e.name = name;
            e.type = FieldType<M>::value;
            e.flags = FieldFlags<M>::value;
            e.offset = uint32_t(at - base);
            m_Entries.push_back(e);
        }

        struct Entry {
            const char* name;
            uint32_t type;
            uint32_t offset;
            uint32_t flags;
        };
        const std::vector<Entry>& Entries() const { return m_Entries; }

    private:
        const T& m_Sample;
        std::vector<Entry> m_Entries;
    };

    /// Which members of T are fields : SHARD_COMPONENT specializes it
    template <typename T>
    struct ComponentTraits {
        static constexpr bool kDescribed = false;
    };

    namespace detail {
        /// The ShardFieldDesc of the fields SHARD_COMPONENT lists for T ; their strings point into `sample` and `options`
        template <typename T, typename Traits = ComponentTraits<T>>
        std::vector<ShardFieldDesc> FieldsOf(const T& sample, std::initializer_list<FieldOptions> options = {})
        {
            FieldList<T> list(sample);
            Traits::Describe(list);
            std::vector<ShardFieldDesc> fields;
            for (const auto& e : list.Entries()) {
                ShardFieldDesc f{};
                f.structSize = sizeof(ShardFieldDesc);
                f.type = e.type;
                f.name = Str(e.name);
                f.offset = e.offset;
                f.flags = e.flags;
                for (const FieldOptions& o : options) {
                    if (o.name != e.name) continue;
                    f.flags = o.flags | e.flags;
                    f.min = o.min;
                    f.max = o.max;
                    f.tooltip = Str(o.tooltip);
                }
                fields.push_back(f);
            }
            return fields;
        }
    }

    /// A component type the host knows, typed : what queries and systems take
    template <typename T>
    struct ComponentType {
        ShardComponentType handle = 0;
        explicit operator bool() const { return handle != 0; }
    };

    /// A type of the engine or of another plugin, whose layout the caller knows (shard_ecs.h documents the engine's)
    template <typename T>
    ComponentType<T> AsComponentType(ShardComponentType handle) { return ComponentType<T>{ handle }; }

    class Components {
    public:
        explicit Components(ShardPluginCtx ctx) : m_Ctx(ctx) {}

        /// Registers T as "<plugin id>.<name>" (from OnLoad). Throws Shard::Error if the host refuses it (the log says why).
        template <typename T>
        ComponentType<T> Register(const std::string& name, std::initializer_list<FieldOptions> options = {})
        {
            static_assert(std::is_trivially_copyable<T>::value && std::is_standard_layout<T>::value, "a component is plain data : trivially copyable, standard layout");
            static_assert(ComponentTraits<T>::kDescribed, "describe the fields of this type with SHARD_COMPONENT");
            static_assert(sizeof(T) <= SHARD_COMPONENT_MAX_SIZE, "a component holds 1024 bytes at most");
            static_assert(alignof(T) <= 16, "a component is aligned on 16 bytes at most");
            const T sample{};
            const std::vector<ShardFieldDesc> fields = detail::FieldsOf<T>(sample, options);
            ShardComponentDesc desc{};
            desc.structSize = sizeof(ShardComponentDesc);
            desc.size = uint32_t(sizeof(T));
            desc.align = uint32_t(alignof(T));
            desc.fieldCount = uint32_t(fields.size());
            desc.name = Str(name);
            desc.fields = fields.data();
            desc.defaults = &sample;
            ComponentType<T> type;
            detail::Check(Api().register_type(m_Ctx, &desc, &type.handle), "component type '" + name + "'");
            return type;
        }

        /// A blob of `size` bytes, zeroed. Throws Shard::Error over the memory budget, or in an engine older than ABI 1.2. thread : any
        Blob CreateBlob(uint64_t size) const
        {
            Blob blob;
            detail::Check(Blobs().blob_create(m_Ctx, size, &blob.handle), "a blob");
            return blob;
        }
        /// Its bytes (null if the blob is not one of this plugin's) ; valid until it is resized or freed. thread : any
        void* BlobData(Blob blob, uint64_t* size = nullptr) const
        {
            void* data = nullptr;
            uint64_t n = 0;
            if (Blobs().blob_data(m_Ctx, blob.handle, &data, &n) != SHARD_OK) return nullptr;
            if (size) *size = n;
            return data;
        }
        bool ResizeBlob(Blob blob, uint64_t size) const { return Blobs().blob_resize(m_Ctx, blob.handle, size) == SHARD_OK; }
        bool FreeBlob(Blob blob) const { return Blobs().blob_free(m_Ctx, blob.handle) == SHARD_OK; }

        /// Sets a string field of a component : `field` is the member itself (in the block of an entity, or in a copy). Throws Shard::Error over
        /// the memory budget, or in an engine older than ABI 1.3. thread : any
        void SetText(Text& field, std::string_view text) const
        {
            detail::Check(Texts().string_set(m_Ctx, &field.handle, Str(text)), "a text");
        }
        /// The text of a string field (empty if it holds none). thread : any
        std::string GetText(Text field) const
        {
            ShardStr out{ nullptr, 0 };
            if (Texts().string_get(m_Ctx, field.handle, &out) != SHARD_OK) return std::string();
            return ToString(out);
        }

        /// A type by its full name ("com.acme.weather.Cloud", "shard.LocalTransform") : empty if there is none
        template <typename T>
        ComponentType<T> Find(const std::string& fullName) const
        {
            ComponentType<T> type;
            if (Api().find_type(m_Ctx, Str(fullName), &type.handle) != SHARD_OK) return ComponentType<T>{};
            uint32_t size = 0;
            if (Api().type_layout(m_Ctx, type.handle, &size, nullptr) != SHARD_OK || size != sizeof(T)) return ComponentType<T>{};
            return type;
        }

    private:
        static const ShardComponentsApi& Api() { return detail::Require<ShardComponentsApi>(SHARD_IFACE_COMPONENTS); }
        static const ShardComponentsApi& Blobs()
        {
            const ShardComponentsApi* api = detail::Interface<ShardComponentsApi>(SHARD_IFACE_COMPONENTS, 1);
            if (!api) SHARD_THROW(Error(SHARD_E_UNSUPPORTED, "blobs need shard.components 1.1"));
            return *api;
        }
        static const ShardComponentsApi& Texts()
        {
            const ShardComponentsApi* api = detail::Interface<ShardComponentsApi>(SHARD_IFACE_COMPONENTS, 2);
            if (!api) SHARD_THROW(Error(SHARD_E_UNSUPPORTED, "text fields need shard.components 1.2"));
            return *api;
        }
        ShardPluginCtx m_Ctx;
    };
}

namespace Shard {
    inline std::string Text::Get() const { return Components(detail::PluginHandle()).GetText(*this); }
    inline void Text::Set(std::string_view text) { Components(detail::PluginHandle()).SetText(*this, text); }
}

// Iterates a sequence (a)(b)(c) : the classic trick of two macros that call each other
#define SHARD_DETAIL_CAT(a, b) SHARD_DETAIL_CAT_(a, b)
#define SHARD_DETAIL_CAT_(a, b) a##b
#define SHARD_DETAIL_FIELD_A(member) fields.Add(#member, &Self::member); SHARD_DETAIL_FIELD_B
#define SHARD_DETAIL_FIELD_B(member) fields.Add(#member, &Self::member); SHARD_DETAIL_FIELD_A
#define SHARD_DETAIL_FIELD_A_END
#define SHARD_DETAIL_FIELD_B_END

/// Declares which members of `Type` are its fields : SHARD_COMPONENT(Spin, (speed)(angle)). At global scope, `Type` fully qualified.
#define SHARD_COMPONENT(Type, sequence)                                                                     \
    template <> struct Shard::ComponentTraits<Type> {                                                       \
        static constexpr bool kDescribed = true;                                                            \
        static void Describe(::Shard::FieldList<Type>& fields)                                              \
        {                                                                                                   \
            using Self = Type;                                                                              \
            SHARD_DETAIL_CAT(SHARD_DETAIL_FIELD_A sequence, _END)                                           \
        }                                                                                                   \
    };

// Shard C++ SDK : the fields of a script, declared where the member is.
//
//   class Gun : public shard::Script {
//   public:
//       SHARD_FIELD(float, power, Range(0.f, 1000.f), Tooltip("Speed of the bullet")) = 30.f;
//       SHARD_FIELD(float, lifetime, Range(0.f, 60.f)) = 3.f;
//       SHARD_FIELD(shard::AssetId, mesh, Tooltip("The mesh to shoot"));
//       SHARD_FIELD(int, ammo, Runtime()) = 12;                  // shown by the inspector, not saved
//       float cooldown = 0.f;                                    // not a field : it stays out of the inspector and the world file
//   };
//
//   // gun.cpp
//   SHARD_REGISTER_SCRIPT(Gun)
//
// The macro declares the member (`Type name`, so the default value goes after it, as for any member) and, in front of it, a one byte member that
// tells the SDK its name, its type, where it is in the object and its options. Nothing else lists the field : no sequence in the .cpp, no
// generated file. Options are the names below, written bare inside the macro:
//
//   Range(min, max)     the bounds of the inspector's widget
//   Tooltip("text")     what the inspector shows when the mouse rests on the field
//   Runtime()           the inspector shows it, read only, and the world file does not save it
//   Hidden()            neither shown nor saved (the object keeps it; not for a string)
//
// The type is any of the field types of shard/cpp/component.hpp (numbers, bool, Vec2/3/4, Quat, Color, Text, AssetId, std::string, Actor, Blob).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <shard/cpp/component.hpp>
#include <shard/cpp/detail.hpp>

namespace shard {

    /// What the options of a field add to it
    struct FieldSpec {
        uint32_t flags = 0;
        float min = 0.f, max = 0.f;
        std::string tooltip;
    };

    /// The options of SHARD_FIELD : `using namespace shard::field` brings them in scope, the macro does it for its arguments
    namespace field {
        struct Range {
            float min, max;
            Range(float lowest, float highest) : min(lowest), max(highest) {}
            void Apply(FieldSpec& spec) const { spec.min = min; spec.max = max; }
        };
        struct Tooltip {
            const char* text;
            explicit Tooltip(const char* hint) : text(hint) {}
            void Apply(FieldSpec& spec) const { spec.tooltip = text ? text : ""; }
        };
        struct Runtime {
            void Apply(FieldSpec& spec) const { spec.flags |= SHARD_FIELD_RUNTIME; }
        };
        struct Hidden {
            void Apply(FieldSpec& spec) const { spec.flags |= SHARD_FIELD_PRIVATE; }
        };
        /// SHARD_FIELD ends its options with it, so that the macro never has an empty "..." (an error in C++17 with -pedantic)
        struct End {
            void Apply(FieldSpec&) const {}
        };
    }

    template <typename... Options>
    FieldSpec MakeFieldSpec(const Options&... options)
    {
        FieldSpec spec;
        (options.Apply(spec), ...);
        return spec;
    }

    namespace detail {

        /// A field SHARD_FIELD declared, as the constructor of its marker found it
        struct DeclaredField {
            const char* name;
            uint32_t type;
            uint32_t flags;
            uint32_t offset;
            const FieldSpec* spec;
        };

        /// Collects the markers of an object while the SDK builds a sample of its class (Scripts::Register) : the objects the host builds later find
        /// no collector and their markers do nothing
        struct FieldCollector {
            /// The address of the object, as a number : handing a pointer to storage that is not built yet to a function is what GCC's
            /// -Wmaybe-uninitialized objects to (an error under -Werror)
            FieldCollector(uintptr_t objectStart, size_t objectSize) : base(objectStart), size(objectSize) {}
            uintptr_t base;
            size_t size;
            std::vector<DeclaredField> fields;
        };

        inline std::atomic<FieldCollector*>& ActiveFieldCollector()
        {
            static std::atomic<FieldCollector*> active{ nullptr };
            return active;
        }

        struct FieldCollectScope {
            explicit FieldCollectScope(FieldCollector& collector) { ActiveFieldCollector().store(&collector); }
            ~FieldCollectScope() { ActiveFieldCollector().store(nullptr); }
            FieldCollectScope(const FieldCollectScope&) = delete;
            FieldCollectScope& operator=(const FieldCollectScope&) = delete;
        };

        /// The one byte member SHARD_FIELD puts in front of a field. Copying it does nothing (a copy of an object has the same fields).
        struct FieldMarker {
            using SpecFn = const FieldSpec& (*)();
            FieldMarker(const char* name, uint32_t type, uint32_t flags, const void* member, SpecFn spec)
            {
                FieldCollector* collector = ActiveFieldCollector().load(std::memory_order_acquire);
                if (!collector) return;
                const uintptr_t at = reinterpret_cast<uintptr_t>(member);
                if (at < collector->base || at >= collector->base + collector->size) return;     // a member of some other object built meanwhile
                collector->fields.push_back(DeclaredField{ name, type, flags, uint32_t(at - collector->base), &spec() });
            }
        };

        template <typename T>
        constexpr uint32_t DeclaredFieldType()
        {
            static_assert(FieldType<T>::value != 0, "this member type has no SHARD_FIELD_* : specialize shard::FieldType for it");
            return FieldType<T>::value;
        }

        /// The ShardFieldDesc of the declared fields, after `fields` (a name already there is a mistake of the script's author)
        inline void AppendDeclaredFields(const FieldCollector& collector, std::vector<ShardFieldDesc>& fields)
        {
            for (const DeclaredField& declared : collector.fields) {
                for (const ShardFieldDesc& other : fields)
                    if (View(other.name) == declared.name)
                        throw Error(SHARD_E_ALREADY_EXISTS, std::string("the field '") + declared.name + "' is declared twice");
                ShardFieldDesc f{};
                f.structSize = sizeof(ShardFieldDesc);
                f.type = declared.type;
                f.name = Str(declared.name);
                f.offset = declared.offset;
                f.flags = declared.flags | declared.spec->flags;
                f.min = declared.spec->min;
                f.max = declared.spec->max;
                f.tooltip = Str(declared.spec->tooltip);
                fields.push_back(f);
            }
        }
    }
}

/// Declares a field of a script : `SHARD_FIELD(float, power, Range(0.f, 1000.f)) = 30.f;` (options are optional, see the top of this file)
#define SHARD_FIELD(...) SHARD_DETAIL_FIELD_EXPAND(SHARD_DETAIL_FIELD(__VA_ARGS__, End()))
#define SHARD_DETAIL_FIELD_EXPAND(x) x
#define SHARD_DETAIL_FIELD(Type, name, ...)                                                                               \
    ::shard::detail::FieldMarker shardFieldMarker_##name##_{                                                              \
        #name, ::shard::detail::DeclaredFieldType<Type>(), ::shard::FieldFlags<Type>::value, &name,                       \
        []() -> const ::shard::FieldSpec& {                                                                               \
            using namespace ::shard::field;                                                                               \
            static const ::shard::FieldSpec spec = ::shard::MakeFieldSpec(__VA_ARGS__);                                   \
            return spec;                                                                                                  \
        } };                                                                                                              \
    Type name

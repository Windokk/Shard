// Shard C++ SDK : what exists in the engine and in the plugins, described at run time ; and functions to be called by name (shard_reflect.h).
//
//   // a visual scripting editor lists what it can wire together
//   for (const auto& type : Shard::Reflect::Types(Shard::TypeKind::Component))
//       for (const auto& field : type.fields) AddPin(type.name, field.name, field.type);
//   for (const auto& fn : Shard::Reflect::Functions()) AddNode(fn.category, fn.name, fn.params, fn.results);
//
//   // a plugin offers a function to the graphs, the UI bindings and the console
//   ctx.Reflect().AddFunction("nav.set_destination", "AI/Navigation", "Sends an agent to a point",
//       { { "agent", ValueType::Actor }, { "target", ValueType::Vec3 } }, { { "ok", ValueType::Bool } },
//       [](const std::vector<Shard::Value>& in, std::vector<Shard::Value>& out) { out[0] = Shard::Value(Navigate(in[0].AsActor(), in[1].AsVec3())); return true; });
//
//   auto out = Shard::Reflect::Call("math.add", { Shard::Value(1.f), Shard::Value(2.f) });         // std::optional<std::vector<Value>>
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <shard/shard_reflect.h>
#include <shard/cpp/actor.hpp>
#include <shard/cpp/detail.hpp>
#include <shard/cpp/math.hpp>

namespace Shard {

    enum class ValueType : uint32_t {
        None = SHARD_VALUE_NONE, Bool = SHARD_VALUE_BOOL, I32 = SHARD_VALUE_I32, I64 = SHARD_VALUE_I64, F32 = SHARD_VALUE_F32, F64 = SHARD_VALUE_F64,
        Vec2 = SHARD_VALUE_VEC2, Vec3 = SHARD_VALUE_VEC3, Vec4 = SHARD_VALUE_VEC4, Quat = SHARD_VALUE_QUAT, Color = SHARD_VALUE_COLOR,
        String = SHARD_VALUE_STRING, Actor = SHARD_VALUE_ACTOR, Asset = SHARD_VALUE_ASSET, Json = SHARD_VALUE_JSON, Enum = SHARD_VALUE_ENUM, Any = SHARD_VALUE_ANY,
    };

    enum class TypeKind : uint32_t { Component = SHARD_TYPE_COMPONENT, Event = SHARD_TYPE_EVENT };

    /// One value of the ABI's flat ShardValue, owning its text
    class Value {
    public:
        Value() = default;
        Value(bool v) : m_Type(ValueType::Bool), m_Int(v ? 1 : 0) {}
        Value(int32_t v) : m_Type(ValueType::I32), m_Int(v) {}
        Value(int64_t v) : m_Type(ValueType::I64), m_Int(v) {}
        Value(float v) : m_Type(ValueType::F32), m_Float{ v, 0.f, 0.f, 0.f } {}
        Value(double v) : m_Type(ValueType::F64), m_Double(v) {}
        Value(Vec2 v) : m_Type(ValueType::Vec2), m_Float{ v.x, v.y, 0.f, 0.f } {}
        Value(Vec3 v) : m_Type(ValueType::Vec3), m_Float{ v.x, v.y, v.z, 0.f } {}
        Value(Vec4 v) : m_Type(ValueType::Vec4), m_Float{ v.x, v.y, v.z, v.w } {}
        Value(Quat v) : m_Type(ValueType::Quat), m_Float{ v.x, v.y, v.z, v.w } {}
        Value(Color v) : m_Type(ValueType::Color), m_Float{ v.r, v.g, v.b, v.a } {}
        Value(const char* v) : m_Type(ValueType::String), m_Text(v ? v : "") {}
        Value(std::string v) : m_Type(ValueType::String), m_Text(std::move(v)) {}
        Value(std::string_view v) : m_Type(ValueType::String), m_Text(v) {}
        Value(Actor v) : m_Type(ValueType::Actor), m_Int(int64_t(v.Handle())) {}

        static Value Json(std::string text) { Value v; v.m_Type = ValueType::Json; v.m_Text = std::move(text); return v; }
        static Value Enum(std::string name) { Value v; v.m_Type = ValueType::Enum; v.m_Text = std::move(name); return v; }
        static Value Asset(int32_t id) { Value v; v.m_Type = ValueType::Asset; v.m_Int = id; return v; }

        ValueType Type() const { return m_Type; }
        bool IsNone() const { return m_Type == ValueType::None; }
        bool AsBool() const { return Number() != 0.0; }
        int64_t AsInt() const { return IsFloating() ? int64_t(Number()) : m_Int; }
        float AsFloat() const { return float(Number()); }
        double AsDouble() const { return Number(); }
        Vec2 AsVec2() const { return { m_Float[0], m_Float[1] }; }
        Vec3 AsVec3() const { return { m_Float[0], m_Float[1], m_Float[2] }; }
        Vec4 AsVec4() const { return { m_Float[0], m_Float[1], m_Float[2], m_Float[3] }; }
        Quat AsQuat() const { return { m_Float[0], m_Float[1], m_Float[2], m_Float[3] }; }
        Color AsColor() const { return { m_Float[0], m_Float[1], m_Float[2], m_Float[3] }; }
        const std::string& AsString() const { return m_Text; }
        Actor AsActor() const { return Actor(ShardActor(m_Int)); }
        int32_t AsAsset() const { return int32_t(m_Int); }

        /// The ABI's value ; the text points into this object
        ShardValue Raw() const
        {
            ShardValue v{};
            v.type = uint32_t(m_Type);
            v.i = m_Int;
            v.d = m_Double;
            for (int k = 0; k < 4; ++k) v.f[k] = m_Float[k];
            v.s = Str(m_Text);
            return v;
        }
        static Value FromRaw(const ShardValue& v)
        {
            Value out;
            out.m_Type = ValueType(v.type);
            out.m_Int = v.i;
            out.m_Double = v.d;
            for (int k = 0; k < 4; ++k) out.m_Float[k] = v.f[k];
            out.m_Text = ToString(v.s);
            return out;
        }

    private:
        bool IsFloating() const { return m_Type == ValueType::F32 || m_Type == ValueType::F64; }
        double Number() const { return m_Type == ValueType::F32 ? double(m_Float[0]) : m_Type == ValueType::F64 ? m_Double : double(m_Int); }
        ValueType m_Type = ValueType::None;
        int64_t m_Int = 0;
        double m_Double = 0.0;
        float m_Float[4] = {};
        std::string m_Text;
    };

    struct ParamSpec {
        std::string name;
        ValueType type = ValueType::Any;
        bool optional = false;
        std::string tooltip;
    };

    struct FieldEntry {
        std::string name;
        ValueType type = ValueType::Json;
        bool readOnly = false, hidden = false;
        float min = 0.f, max = 0.f;
        std::string tooltip;
        std::vector<std::string> enumNames;
    };

    struct TypeEntry {
        std::string name, plugin;
        TypeKind kind = TypeKind::Component;
        std::vector<FieldEntry> fields;
    };

    struct FunctionEntry {
        std::string name, category, tooltip, plugin;
        uint32_t flags = 0;
        std::vector<ParamSpec> params, results;
    };

    class Reflect {
    public:
        explicit Reflect(ShardPluginCtx ctx) : m_Ctx(ctx) {}

        using FunctionFn = std::function<bool(const std::vector<Value>& args, std::vector<Value>& results)>;

        /// Registers a function (from OnLoad or later) ; it goes with the plugin. `fn` fills `results` (already sized) and returns false for an error.
        /// Throws Shard::Error if the name is taken or the signature is wrong.
        void AddFunction(std::string name, std::string category, std::string tooltip, std::vector<ParamSpec> params, std::vector<ParamSpec> results, FunctionFn fn,
                         uint32_t flags = 0) const
        {
            struct Kept { FunctionFn fn; };
            auto* kept = detail::Keep(Kept{ std::move(fn) });
            auto convert = [](const std::vector<ParamSpec>& in) {
                std::vector<ShardParamDesc> out;
                for (const ParamSpec& p : in) {
                    ShardParamDesc d{};
                    d.structSize = sizeof(ShardParamDesc);
                    d.type = uint32_t(p.type);
                    d.flags = p.optional ? SHARD_PARAM_OPTIONAL : 0u;
                    d.name = Str(p.name);
                    d.tooltip = Str(p.tooltip);
                    out.push_back(d);
                }
                return out;
            };
            const std::vector<ShardParamDesc> in = convert(params), out = convert(results);
            ShardFunctionDesc desc{};
            desc.structSize = sizeof(ShardFunctionDesc);
            desc.flags = flags;
            desc.paramCount = uint32_t(in.size());
            desc.resultCount = uint32_t(out.size());
            desc.name = Str(name);
            desc.category = Str(category);
            desc.tooltip = Str(tooltip);
            desc.params = in.empty() ? nullptr : in.data();
            desc.results = out.empty() ? nullptr : out.data();
            desc.user = kept;
            desc.call = [](void* user, ShardPluginCtx, const ShardValue* args, uint32_t argCount, ShardValue* results, uint32_t resultCount) -> ShardResult {
                auto& k = *static_cast<Kept*>(user);
                return detail::Guard(detail::PluginHandle(), "a reflected function", [&] {
                    std::vector<Value> a;
                    for (uint32_t i = 0; i < argCount; ++i) a.push_back(Value::FromRaw(args[i]));
                    std::vector<Value> r(resultCount);
                    if (!k.fn(a, r)) SHARD_THROW(Error(SHARD_E_INVALID_ARG, "the function refused its arguments"));
                    // The texts of the results must outlive the call until the host copied them
                    thread_local std::vector<Value> keep;
                    keep = std::move(r);
                    for (uint32_t i = 0; i < resultCount && i < keep.size(); ++i) results[i] = keep[i].Raw();
                });
            };
            detail::Check(Api().function_add(m_Ctx, &desc), "function '" + name + "'");
        }

        /// Calls a function by name (thread : script or main, the one the function is made for)
        static std::optional<std::vector<Value>> Call(std::string_view name, const std::vector<Value>& args)
        {
            ShardFunctionInfo info{};
            info.structSize = sizeof(ShardFunctionInfo);
            if (Api().function_find(Str(name), &info) != SHARD_OK) return std::nullopt;
            std::vector<ShardValue> raw;
            for (const Value& v : args) raw.push_back(v.Raw());
            std::vector<ShardValue> out(info.resultCount);
            if (Api().function_call(detail::PluginHandle(), Str(name), raw.data(), uint32_t(raw.size()), out.data(), uint32_t(out.size())) != SHARD_OK) return std::nullopt;
            std::vector<Value> values;
            for (const ShardValue& v : out) values.push_back(Value::FromRaw(v));
            return values;
        }

        /// The functions registered now (every plugin's), sorted by name
        static std::vector<FunctionEntry> Functions()
        {
            std::vector<FunctionEntry> list;
            const uint32_t count = Api().function_count();
            for (uint32_t i = 0; i < count; ++i) {
                ShardFunctionInfo info{};
                info.structSize = sizeof(ShardFunctionInfo);
                if (Api().function_at(i, &info) != SHARD_OK) continue;
                FunctionEntry e;
                e.name = ToString(info.name); e.category = ToString(info.category); e.tooltip = ToString(info.tooltip); e.plugin = ToString(info.plugin); e.flags = info.flags;
                for (int result = 0; result < 2; ++result) {
                    const uint32_t n = result ? info.resultCount : info.paramCount;
                    for (uint32_t k = 0; k < n; ++k) {
                        ShardParamDesc p{};
                        p.structSize = sizeof(ShardParamDesc);
                        if (Api().function_param_at(info.name, result, k, &p) != SHARD_OK) continue;
                        (result ? e.results : e.params).push_back(ParamSpec{ ToString(p.name), ValueType(p.type), (p.flags & SHARD_PARAM_OPTIONAL) != 0, ToString(p.tooltip) });
                    }
                }
                list.push_back(std::move(e));
            }
            return list;
        }

        /// The component types (the engine's and the plugins') or the described events, with their fields (thread : main)
        static std::vector<TypeEntry> Types(TypeKind kind)
        {
            std::vector<TypeEntry> list;
            const uint32_t count = Api().type_count(uint32_t(kind));
            for (uint32_t i = 0; i < count; ++i) {
                ShardTypeInfo info{};
                info.structSize = sizeof(ShardTypeInfo);
                if (Api().type_at(uint32_t(kind), i, &info) != SHARD_OK) continue;
                TypeEntry e;
                e.name = ToString(info.name); e.plugin = ToString(info.plugin); e.kind = kind;
                for (uint32_t k = 0; k < info.fieldCount; ++k) {
                    ShardFieldInfo f{};
                    f.structSize = sizeof(ShardFieldInfo);
                    if (Api().field_at(uint32_t(kind), info.name, k, &f) != SHARD_OK) continue;
                    FieldEntry field;
                    field.name = ToString(f.name); field.type = ValueType(f.valueType); field.min = f.min; field.max = f.max; field.tooltip = ToString(f.tooltip);
                    field.readOnly = (f.flags & SHARD_FIELD_INFO_READONLY) != 0;
                    field.hidden = (f.flags & SHARD_FIELD_INFO_HIDDEN) != 0;
                    for (uint32_t v = 0; v < f.enumCount; ++v) {
                        ShardStr name{ nullptr, 0 };
                        if (Api().field_enum_at(uint32_t(kind), info.name, k, v, &name) == SHARD_OK) field.enumNames.push_back(ToString(name));
                    }
                    e.fields.push_back(std::move(field));
                }
                list.push_back(std::move(e));
            }
            return list;
        }

    private:
        static const ShardReflectApi& Api() { return detail::Require<ShardReflectApi>(SHARD_IFACE_REFLECT); }
        ShardPluginCtx m_Ctx;
    };
}

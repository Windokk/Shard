#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>

#include <shard/shard_reflect.h>

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/plugins/abi/c_handles.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/components/registry/component_registry.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/event_system.hpp"

// shard.reflect : what exists (the component types of the engine and of the plugins, the described events), and the functions plugins register to be
// called by name with typed values.

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        // ---- Types ----

        struct EnumRec { std::vector<std::string> names; };

        struct FieldRec {
            std::string name;
            std::string tooltip;
            uint32_t valueType = SHARD_VALUE_JSON;
            uint32_t flags = 0;
            float min = 0.0f, max = 0.0f;
            std::vector<std::string> enumNames;
        };

        struct TypeRec {
            std::string name;
            std::string plugin;
            uint32_t kind = SHARD_TYPE_COMPONENT;
            uint32_t flags = 0;
            std::vector<FieldRec> fields;
            bool fieldsKnown = false;
        };

        uint32_t ValueTypeOf(TypeID type)
        {
            switch (type) {
                case TypeID::Int8: case TypeID::Int16: case TypeID::Int32: case TypeID::UInt8: case TypeID::UInt16: case TypeID::UInt32: return SHARD_VALUE_I32;
                case TypeID::Int64: case TypeID::UInt64: return SHARD_VALUE_I64;
                case TypeID::Float: return SHARD_VALUE_F32;
                case TypeID::Double: return SHARD_VALUE_F64;
                case TypeID::Bool: return SHARD_VALUE_BOOL;
                case TypeID::Vec2: return SHARD_VALUE_VEC2;
                case TypeID::Vec3: return SHARD_VALUE_VEC3;
                case TypeID::Vec4: return SHARD_VALUE_VEC4;
                case TypeID::Quat: return SHARD_VALUE_QUAT;
                case TypeID::ColorRGB: case TypeID::ColorRGBA: return SHARD_VALUE_COLOR;
                case TypeID::String: case TypeID::CString: return SHARD_VALUE_STRING;
                case TypeID::Enum: return SHARD_VALUE_ENUM;
                case TypeID::Asset: return SHARD_VALUE_ASSET;
                default: return SHARD_VALUE_JSON;
            }
        }

        void ReadFields(const ClassDescriptor& descriptor, TypeRec& rec)
        {
            rec.fields.clear();
            for (const FieldInfo* field : descriptor.fields) {
                if (!field || !field->name) continue;
                FieldRec f;
                f.name = field->name;
                f.tooltip = field->tooltip ? field->tooltip : "";
                f.valueType = field->container ? uint32_t(SHARD_VALUE_JSON) : ValueTypeOf(field->type);
                f.flags = (field->flags & ReadOnly ? SHARD_FIELD_INFO_READONLY : 0u) | (field->flags & Editable ? 0u : SHARD_FIELD_INFO_HIDDEN);
                f.min = field->min;
                f.max = field->max;
                if (field->enumDesc) {
                    f.valueType = SHARD_VALUE_ENUM;
                    for (const EnumValueInfo& v : field->enumDesc->values) f.enumNames.push_back(v.name ? v.name : "");
                }
                rec.fields.push_back(std::move(f));
            }
            rec.fieldsKnown = true;
        }

        std::mutex gTypesMutex;
        std::map<std::string, TypeRec> gBuiltinTypes;       // the engine's own : their descriptors never change

        Core::IEngineContext* Engine() { return Core::gSharedEnginePtr; }

        // The fields of a component type : read from an instance (a builtin one needs an actor to be made on)
        bool DescribeComponent(const std::string& name, TypeRec& rec)
        {
            Core::IEngineContext* engine = Engine();
            auto& registry = Objects::Components::gSharedComponentRegistry;
            std::shared_ptr<Objects::Components::Component> component;
            std::shared_ptr<Objects::Actor> holder;
            if (registry.IsBuiltinComponent(name)) {
                if (!engine) return false;
                holder = Core::Object::CreateWithContext<Objects::Actor>(engine, std::string("(reflect)"), engine);
                component = registry.CreateBuiltinComponent(name, engine, holder, 0);
            } else {
                component = registry.CreateComponentByName(name);
            }
            if (!component) return false;
            if (const ClassDescriptor* descriptor = component->GetDescriptor()) ReadFields(*descriptor, rec);
            else rec.fieldsKnown = true;
            component.reset();          // never attached to a world : it just goes
            if (holder) holder->Destroy();
            return true;
        }

        std::string PluginOf(const std::string& name)
        {
            const size_t dot = name.rfind('.');
            return dot == std::string::npos ? std::string() : name.substr(0, dot);
        }

        // The names of the types of a kind, sorted
        std::vector<std::string> TypeNames(uint32_t kind)
        {
            std::vector<std::string> names;
            if (kind == SHARD_TYPE_COMPONENT) {
                auto& registry = Objects::Components::gSharedComponentRegistry;
                names = registry.BuiltinNames();
                for (const auto& kv : registry.GetAll()) names.push_back(kv.first);
                std::sort(names.begin(), names.end());
                names.erase(std::unique(names.begin(), names.end()), names.end());
            } else if (kind == SHARD_TYPE_EVENT) {
                Core::IEngineContext* engine = Engine();
                if (engine && engine->GetEventDispatcher()) names = engine->GetEventDispatcher()->describedNames();
            }
            return names;
        }

        // The record of a type, with its fields if `fields`. false : no such type
        bool FindType(uint32_t kind, const std::string& name, bool fields, TypeRec& rec)
        {
            rec = TypeRec{};
            rec.name = name;
            rec.kind = kind;
            if (kind == SHARD_TYPE_COMPONENT) {
                auto& registry = Objects::Components::gSharedComponentRegistry;
                const bool builtin = registry.IsBuiltinComponent(name);
                if (!builtin && !registry.IsRegistered(name)) return false;
                rec.plugin = builtin ? std::string() : PluginOf(name);
                if (!fields) return true;
                if (builtin) {
                    {
                        std::lock_guard<std::mutex> lock(gTypesMutex);
                        auto it = gBuiltinTypes.find(name);
                        if (it != gBuiltinTypes.end()) { rec = it->second; return true; }
                    }
                    if (!DescribeComponent(name, rec)) return false;
                    rec.plugin.clear();
                    std::lock_guard<std::mutex> lock(gTypesMutex);
                    gBuiltinTypes[name] = rec;
                    return true;
                }
                return DescribeComponent(name, rec);
            }
            if (kind == SHARD_TYPE_EVENT) {
                Core::IEngineContext* engine = Engine();
                Events::EventDispatcher* dispatcher = engine ? engine->GetEventDispatcher() : nullptr;
                const Events::NamedEventLayout* layout = dispatcher ? dispatcher->namedLayout(name) : nullptr;
                if (!layout) return false;
                rec.plugin = PluginOf(name);
                for (const auto& field : layout->fields) {
                    FieldRec f;
                    f.name = field.name;
                    switch (field.type) {
                        case Events::NamedEventLayout::FieldType::F32: f.valueType = SHARD_VALUE_F32; break;
                        case Events::NamedEventLayout::FieldType::F64: f.valueType = SHARD_VALUE_F64; break;
                        case Events::NamedEventLayout::FieldType::I32: case Events::NamedEventLayout::FieldType::U32: f.valueType = SHARD_VALUE_I32; break;
                        case Events::NamedEventLayout::FieldType::I64: case Events::NamedEventLayout::FieldType::U64: f.valueType = SHARD_VALUE_I64; break;
                        case Events::NamedEventLayout::FieldType::Bool: f.valueType = SHARD_VALUE_BOOL; break;
                        case Events::NamedEventLayout::FieldType::Vec2: f.valueType = SHARD_VALUE_VEC2; break;
                        case Events::NamedEventLayout::FieldType::Vec3: f.valueType = SHARD_VALUE_VEC3; break;
                        case Events::NamedEventLayout::FieldType::Vec4: f.valueType = SHARD_VALUE_VEC4; break;
                        case Events::NamedEventLayout::FieldType::Quat: f.valueType = SHARD_VALUE_QUAT; break;
                        case Events::NamedEventLayout::FieldType::Color: f.valueType = SHARD_VALUE_COLOR; break;
                    }
                    rec.fields.push_back(std::move(f));
                }
                rec.fieldsKnown = true;
                return true;
            }
            return false;
        }

        // The strings of a returned ShardTypeInfo / ShardFieldInfo must outlive the call : they live in a small ring of the calling thread
        const std::string& Keep(const std::string& s)
        {
            thread_local std::vector<std::string> ring(64);
            thread_local size_t next = 0;
            ring[next] = s;
            const std::string& kept = ring[next];
            next = (next + 1) % ring.size();
            return kept;
        }

        void CopyStruct(const void* full, size_t fullSize, void* out, uint32_t asked)
        {
            std::memcpy(out, full, std::min<size_t>(asked, fullSize));
            *static_cast<uint32_t*>(out) = asked;
        }

        uint32_t SHARD_CALL TypeCount(uint32_t kind)
        {
            try { return uint32_t(TypeNames(kind).size()); } catch (...) { return 0; }
        }

        ShardResult Fill(uint32_t kind, const std::string& name, ShardTypeInfo* out)
        {
            TypeRec rec;
            if (!FindType(kind, name, true, rec)) return SHARD_E_NOT_FOUND;
            ShardTypeInfo info{};
            info.structSize = sizeof(ShardTypeInfo);
            info.kind = kind;
            info.fieldCount = uint32_t(rec.fields.size());
            info.flags = 0;
            info.name = ToStr(Keep(rec.name));
            info.plugin = ToStr(Keep(rec.plugin));
            info.category = ShardStr{ nullptr, 0 };
            CopyStruct(&info, sizeof(info), out, out->structSize);
            return SHARD_OK;
        }

        ShardResult SHARD_CALL TypeAt(uint32_t kind, uint32_t index, ShardTypeInfo* out)
        {
            try {
                if (!out || out->structSize < sizeof(uint32_t)) return SHARD_E_INVALID_ARG;
                const std::vector<std::string> names = TypeNames(kind);
                if (index >= names.size()) return SHARD_E_NOT_FOUND;
                return Fill(kind, names[index], out);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL TypeFind(uint32_t kind, ShardStr name, ShardTypeInfo* out)
        {
            try {
                if (!out || out->structSize < sizeof(uint32_t)) return SHARD_E_INVALID_ARG;
                return Fill(kind, ToString(name), out);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL FieldAt(uint32_t kind, ShardStr type, uint32_t index, ShardFieldInfo* out)
        {
            try {
                if (!out || out->structSize < sizeof(uint32_t)) return SHARD_E_INVALID_ARG;
                TypeRec rec;
                if (!FindType(kind, ToString(type), true, rec)) return SHARD_E_NOT_FOUND;
                if (index >= rec.fields.size()) return SHARD_E_NOT_FOUND;
                const FieldRec& f = rec.fields[index];
                ShardFieldInfo info{};
                info.structSize = sizeof(ShardFieldInfo);
                info.valueType = f.valueType;
                info.flags = f.flags;
                info.enumCount = uint32_t(f.enumNames.size());
                info.min = f.min;
                info.max = f.max;
                info.name = ToStr(Keep(f.name));
                info.tooltip = ToStr(Keep(f.tooltip));
                CopyStruct(&info, sizeof(info), out, out->structSize);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL FieldEnumAt(uint32_t kind, ShardStr type, uint32_t fieldIndex, uint32_t valueIndex, ShardStr* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = ShardStr{ nullptr, 0 };
                TypeRec rec;
                if (!FindType(kind, ToString(type), true, rec)) return SHARD_E_NOT_FOUND;
                if (fieldIndex >= rec.fields.size() || valueIndex >= rec.fields[fieldIndex].enumNames.size()) return SHARD_E_NOT_FOUND;
                *out = ToStr(Keep(rec.fields[fieldIndex].enumNames[valueIndex]));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // ---- Functions ----

        struct ParamRec { std::string name, tooltip; uint32_t type = SHARD_VALUE_NONE, flags = 0; };

        struct FunctionRec {
            PluginOwner owner = 0;
            std::string name, category, tooltip, plugin;
            uint32_t flags = 0;
            std::vector<ParamRec> params, results;
            ShardFunctionFn call = nullptr;
            void* user = nullptr;
            std::atomic<bool> alive{ true };
        };

        std::mutex gFunctionsMutex;
        std::map<std::string, std::shared_ptr<FunctionRec>> gFunctions;         // by name, sorted
        std::map<PluginOwner, bool> gHooked;

        std::vector<ParamRec> ReadParams(const ShardParamDesc* params, uint32_t count, bool& ok)
        {
            std::vector<ParamRec> out;
            bool optional = false;
            for (uint32_t i = 0; i < count; ++i) {
                if (params[i].structSize < offsetof(ShardParamDesc, tooltip) || params[i].type == SHARD_VALUE_NONE || params[i].type > SHARD_VALUE_ANY) { ok = false; return out; }
                ParamRec p;
                p.name = ToString(params[i].name);
                if (params[i].structSize >= sizeof(ShardParamDesc)) p.tooltip = ToString(params[i].tooltip);
                p.type = params[i].type;
                p.flags = params[i].flags;
                if (p.flags & SHARD_PARAM_OPTIONAL) optional = true;
                else if (optional) { ok = false; return out; }           // a required parameter after an optional one
                out.push_back(std::move(p));
            }
            return out;
        }

        ShardResult SHARD_CALL FunctionAdd(ShardPluginCtx handle, const ShardFunctionDesc* desc)
        {
            try {
                if (!desc || desc->structSize < offsetof(ShardFunctionDesc, user) || !desc->call || desc->name.size == 0) return SHARD_E_INVALID_ARG;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                if ((desc->paramCount && !desc->params) || (desc->resultCount && !desc->results)) return SHARD_E_INVALID_ARG;
                bool ok = true;
                auto rec = std::make_shared<FunctionRec>();
                rec->owner = PluginOwner(handle);
                rec->plugin = ctx->Id();
                rec->name = ToString(desc->name);
                rec->category = ToString(desc->category);
                rec->tooltip = ToString(desc->tooltip);
                rec->flags = desc->flags;
                rec->params = ReadParams(desc->params, desc->paramCount, ok);
                if (ok) rec->results = ReadParams(desc->results, desc->resultCount, ok);
                if (!ok) return SHARD_E_INVALID_ARG;
                rec->call = desc->call;
                rec->user = desc->user;
                {
                    std::lock_guard<std::mutex> lock(gFunctionsMutex);
                    if (gFunctions.count(rec->name)) return SHARD_E_ALREADY_EXISTS;
                    gFunctions[rec->name] = rec;
                    if (!gHooked[PluginOwner(handle)]) {
                        gHooked[PluginOwner(handle)] = true;
                        const PluginOwner owner = PluginOwner(handle);
                        ctx->OnRelease("reflect", [owner] {
                            std::lock_guard<std::mutex> lock(gFunctionsMutex);
                            for (auto it = gFunctions.begin(); it != gFunctions.end();) {
                                if (it->second->owner == owner) { it->second->alive = false; it = gFunctions.erase(it); }
                                else ++it;
                            }
                            gHooked.erase(owner);
                        });
                    }
                }
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL FunctionRemove(ShardPluginCtx handle, ShardStr name)
        {
            try {
                std::lock_guard<std::mutex> lock(gFunctionsMutex);
                auto it = gFunctions.find(ToString(name));
                if (it == gFunctions.end() || it->second->owner != PluginOwner(handle)) return SHARD_E_NOT_FOUND;
                it->second->alive = false;
                gFunctions.erase(it);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        uint32_t SHARD_CALL FunctionCount()
        {
            std::lock_guard<std::mutex> lock(gFunctionsMutex);
            return uint32_t(gFunctions.size());
        }

        void FillInfo(const FunctionRec& f, ShardFunctionInfo* out)
        {
            ShardFunctionInfo info{};
            info.structSize = sizeof(ShardFunctionInfo);
            info.flags = f.flags;
            info.paramCount = uint32_t(f.params.size());
            info.resultCount = uint32_t(f.results.size());
            // The strings are the record's : valid while the function is registered
            info.name = ToStr(f.name);
            info.category = ToStr(f.category);
            info.tooltip = ToStr(f.tooltip);
            info.plugin = ToStr(f.plugin);
            CopyStruct(&info, sizeof(info), out, out->structSize);
        }

        ShardResult SHARD_CALL FunctionAt(uint32_t index, ShardFunctionInfo* out)
        {
            try {
                if (!out || out->structSize < sizeof(uint32_t)) return SHARD_E_INVALID_ARG;
                std::lock_guard<std::mutex> lock(gFunctionsMutex);
                if (index >= gFunctions.size()) return SHARD_E_NOT_FOUND;
                auto it = gFunctions.begin();
                std::advance(it, index);
                FillInfo(*it->second, out);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL FunctionFind(ShardStr name, ShardFunctionInfo* out)
        {
            try {
                if (!out || out->structSize < sizeof(uint32_t)) return SHARD_E_INVALID_ARG;
                std::lock_guard<std::mutex> lock(gFunctionsMutex);
                auto it = gFunctions.find(ToString(name));
                if (it == gFunctions.end()) return SHARD_E_NOT_FOUND;
                FillInfo(*it->second, out);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL FunctionParamAt(ShardStr name, int32_t result, uint32_t index, ShardParamDesc* out)
        {
            try {
                if (!out || out->structSize < sizeof(uint32_t)) return SHARD_E_INVALID_ARG;
                std::lock_guard<std::mutex> lock(gFunctionsMutex);
                auto it = gFunctions.find(ToString(name));
                if (it == gFunctions.end()) return SHARD_E_NOT_FOUND;
                const auto& list = result ? it->second->results : it->second->params;
                if (index >= list.size()) return SHARD_E_NOT_FOUND;
                ShardParamDesc p{};
                p.structSize = sizeof(ShardParamDesc);
                p.type = list[index].type;
                p.flags = list[index].flags;
                p.name = ToStr(list[index].name);
                p.tooltip = ToStr(list[index].tooltip);
                CopyStruct(&p, sizeof(p), out, out->structSize);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        bool IsNumber(uint32_t type) { return type == SHARD_VALUE_BOOL || type == SHARD_VALUE_I32 || type == SHARD_VALUE_I64 || type == SHARD_VALUE_F32 || type == SHARD_VALUE_F64; }
        bool IsText(uint32_t type) { return type == SHARD_VALUE_STRING || type == SHARD_VALUE_JSON || type == SHARD_VALUE_ENUM; }
        bool IsVector(uint32_t type) { return type >= SHARD_VALUE_VEC2 && type <= SHARD_VALUE_COLOR; }

        double AsNumber(const ShardValue& v)
        {
            switch (v.type) {
                case SHARD_VALUE_F32: return double(v.f[0]);
                case SHARD_VALUE_F64: return v.d;
                default: return double(v.i);
            }
        }

        // `v` converted to what the parameter takes : false if it cannot be
        bool Convert(const ShardValue& v, uint32_t want, ShardValue& out)
        {
            out = v;
            if (want == SHARD_VALUE_ANY || v.type == want) return true;
            if (IsNumber(want) && IsNumber(v.type)) {
                const double n = AsNumber(v);
                out = ShardValue{};
                out.type = want;
                switch (want) {
                    case SHARD_VALUE_BOOL: out.i = n != 0.0; break;
                    case SHARD_VALUE_I32: case SHARD_VALUE_I64: out.i = int64_t(std::llround(n)); break;
                    case SHARD_VALUE_F32: out.f[0] = float(n); break;
                    default: out.d = n; break;
                }
                return true;
            }
            if (IsText(want) && IsText(v.type)) { out.type = want; return true; }
            if (IsVector(want) && IsVector(v.type) && v.type != SHARD_VALUE_VEC2 && v.type != SHARD_VALUE_VEC3) { out.type = want; return true; }     // quat <-> vec4 <-> colour
            if (want == SHARD_VALUE_ACTOR && v.type == SHARD_VALUE_I64) { out.type = want; return true; }
            return false;
        }

        ShardResult SHARD_CALL FunctionCall(ShardPluginCtx handle, ShardStr name, const ShardValue* args, uint32_t argCount, ShardValue* results, uint32_t resultCount)
        {
            try {
                if ((argCount && !args) || (resultCount && !results)) return SHARD_E_INVALID_ARG;
                PluginContext* caller = FindContext(handle);
                if (!caller) return SHARD_E_STALE_HANDLE;
                std::shared_ptr<FunctionRec> f;
                {
                    std::lock_guard<std::mutex> lock(gFunctionsMutex);
                    auto it = gFunctions.find(ToString(name));
                    if (it == gFunctions.end()) return SHARD_E_NOT_FOUND;
                    f = it->second;
                }
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                size_t required = 0;
                for (const ParamRec& p : f->params) if (!(p.flags & SHARD_PARAM_OPTIONAL)) ++required;
                if (argCount < required || argCount > f->params.size() || resultCount < f->results.size()) return SHARD_E_INVALID_ARG;

                std::vector<ShardValue> converted(argCount);
                for (uint32_t i = 0; i < argCount; ++i)
                    if (!Convert(args[i], f->params[i].type, converted[i])) return SHARD_E_INVALID_ARG;
                for (uint32_t i = 0; i < resultCount; ++i) results[i] = ShardValue{};

                PluginContext* owner = FindContext(ShardPluginCtx(f->owner));
                if (!owner || !f->alive.load()) return SHARD_E_NOT_FOUND;
                ShardResult r;
                {
                    ActiveScope scope(ActiveCounter(*owner), f->owner);
                    r = f->call(f->user, ShardPluginCtx(f->owner), converted.data(), argCount, results, uint32_t(f->results.size()));
                }
                // Strings handed back stay valid until the function is called again on this thread : copied into a ring
                thread_local std::vector<std::string> ring(32);
                thread_local size_t next = 0;
                for (uint32_t i = 0; i < f->results.size() && i < resultCount; ++i) {
                    if (results[i].type == SHARD_VALUE_NONE) results[i].type = f->results[i].type;
                    if (IsText(results[i].type) && results[i].s.data) {
                        ring[next].assign(results[i].s.data, size_t(results[i].s.size));
                        results[i].s = ToStr(ring[next]);
                        next = (next + 1) % ring.size();
                    }
                }
                return r;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    const void* ReflectApi()
    {
        static const ShardReflectApi api = {
            sizeof(ShardReflectApi),
            &TypeCount, &TypeAt, &TypeFind, &FieldAt, &FieldEnumAt,
            &FunctionAdd, &FunctionRemove, &FunctionCount, &FunctionAt, &FunctionFind, &FunctionParamAt, &FunctionCall };
        return &api;
    }
}

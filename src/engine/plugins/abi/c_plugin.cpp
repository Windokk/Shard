#include "engine/plugins/abi/c_plugin.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <shard/shard_commands.h>
#include <shard/shard_cvars.h>
#include <shard/shard_plugin.h>

#include "engine/core/config/cvar.hpp"
#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Plugins {

    namespace {
        // ShardPluginCtx is the plugin's owner id ; the context is looked up for each call, so a handle that outlived the plugin finds nothing
        std::mutex gContextMutex;
        struct BoundContext {
            PluginContext* ctx = nullptr;
            std::string version;        // the "shard.plugin" interface hands out pointers into this, so it lives as long as the binding
        };
        std::unordered_map<ShardPluginCtx, BoundContext> gContexts;

        // ShardCVar handles : slot + generation, so a variable the host destroyed with the plugin's context fails instead of dangling
        enum class CVarType : uint8_t { Bool, Int, Float, String };
        struct CVarSlot {
            PluginOwner owner = 0;
            CVarType type = CVarType::Bool;
            void* cvar = nullptr;           // a Core::CVar<T>* ; null : free slot
            uint32_t generation = 1;
        };
        std::vector<CVarSlot> gCVarSlots;   // guarded by gContextMutex
        std::unordered_map<ShardPluginCtx, std::vector<std::string>> gCommandNames;     // what each plugin added, to refuse its own duplicates

        // gContextMutex held
        void ReleaseCVarSlots(PluginOwner owner)
        {
            for (CVarSlot& slot : gCVarSlots) {
                if (slot.owner != owner || !slot.cvar) continue;
                slot.cvar = nullptr;
                slot.owner = 0;
                if (++slot.generation == 0) slot.generation = 1;
            }
        }

        void BindContext(PluginOwner owner, PluginContext* ctx)
        {
            std::lock_guard<std::mutex> lock(gContextMutex);
            if (ctx) gContexts[owner] = BoundContext{ ctx, ctx->Manifest().version.ToString() };
            else { gContexts.erase(owner); ReleaseCVarSlots(owner); gCommandNames.erase(owner); }
        }

        PluginContext* FindContext(ShardPluginCtx handle)
        {
            std::lock_guard<std::mutex> lock(gContextMutex);
            auto it = gContexts.find(handle);
            return it == gContexts.end() ? nullptr : it->second.ctx;
        }

        std::string ToString(ShardStr s) { return s.data ? std::string(s.data, size_t(s.size)) : std::string(); }
        ShardStr ToStr(const std::string& s) { return ShardStr{ s.data(), uint64_t(s.size()) }; }

        // ---- The table ----

        void SHARD_CALL HostLog(ShardPluginCtx handle, uint32_t level, ShardStr message)
        {
            try {
                const std::string text = ToString(message);
                if (PluginContext* ctx = FindContext(handle)) {
                    if (level == SHARD_LOG_ERROR) ctx->Error(text);
                    else if (level == SHARD_LOG_WARNING) ctx->Warn(text);
                    else ctx->Log(text);
                    return;
                }
                const auto where = level == SHARD_LOG_ERROR ? Debugging::Level::Error : level == SHARD_LOG_WARNING ? Debugging::Level::Warning : Debugging::Level::Info;
                Debugging::GetLogger().Log(where, "plugin", 0, "[c plugin] " + text);
            } catch (...) {}
        }

        void* SHARD_CALL HostAlloc(ShardPluginCtx handle, uint64_t size, uint64_t align)
        {
            try {
                PluginContext* ctx = FindContext(handle);
                if (!ctx || size == 0) return nullptr;
                return ctx->Memory().Allocate(size_t(size), size_t(align < 16 ? 16 : align));
            } catch (...) { return nullptr; }
        }

        void SHARD_CALL HostFree(ShardPluginCtx handle, void* p)
        {
            try {
                if (!p) return;
                if (PluginContext* ctx = FindContext(handle)) ctx->Memory().Free(p);
            } catch (...) {}
        }

        ShardStr SHARD_CALL PluginId(ShardPluginCtx handle) { PluginContext* c = FindContext(handle); return c ? ToStr(c->Id()) : ShardStr{ nullptr, 0 }; }
        ShardStr SHARD_CALL PluginVersion(ShardPluginCtx handle)
        {
            std::lock_guard<std::mutex> lock(gContextMutex);
            auto it = gContexts.find(handle);
            return it == gContexts.end() ? ShardStr{ nullptr, 0 } : ToStr(it->second.version);
        }
        ShardStr SHARD_CALL PluginDirectory(ShardPluginCtx handle) { PluginContext* c = FindContext(handle); return c ? ToStr(c->Directory()) : ShardStr{ nullptr, 0 }; }

        // ---- shard.cvars ----

        ShardCVar AddCVarSlot(PluginOwner owner, CVarType type, void* cvar)
        {
            std::lock_guard<std::mutex> lock(gContextMutex);
            size_t index = gCVarSlots.size();
            for (size_t i = 0; i < gCVarSlots.size(); ++i) if (!gCVarSlots[i].cvar) { index = i; break; }
            if (index == gCVarSlots.size()) gCVarSlots.emplace_back();
            CVarSlot& slot = gCVarSlots[index];
            slot.owner = owner;
            slot.type = type;
            slot.cvar = cvar;
            return (ShardCVar(slot.generation) << 32) | ShardCVar(index + 1);
        }

        // The variable behind a handle of the given plugin and type, or null with the reason in `result`
        template <typename T>
        Core::CVar<T>* FindCVar(ShardPluginCtx owner, ShardCVar handle, CVarType type, ShardResult& result)
        {
            std::lock_guard<std::mutex> lock(gContextMutex);
            const uint64_t index = (handle & 0xFFFFFFFFull);
            if (index == 0 || index > gCVarSlots.size()) { result = SHARD_E_INVALID_ARG; return nullptr; }
            const CVarSlot& slot = gCVarSlots[size_t(index - 1)];
            if (!slot.cvar || slot.generation != uint32_t(handle >> 32) || slot.owner != owner) { result = SHARD_E_STALE_HANDLE; return nullptr; }
            if (slot.type != type) { result = SHARD_E_INVALID_ARG; return nullptr; }
            return static_cast<Core::CVar<T>*>(slot.cvar);
        }

        Core::CVarFlags ToCVarFlags(uint32_t flags) { return (flags & SHARD_CVAR_READONLY) ? Core::CVarFlags::ReadOnly : Core::CVarFlags::None; }

        template <typename T, typename Make>
        ShardResult CreateCVar(ShardPluginCtx handle, ShardStr name, CVarType type, ShardCVar* out, Make&& make)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!name.data || name.size == 0) return SHARD_E_INVALID_ARG;
                Core::CVar<T>* cvar = make(*ctx, ToString(name));
                if (!cvar) return SHARD_E_ALREADY_EXISTS;
                *out = AddCVarSlot(handle, type, cvar);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL CVarCreateBool(ShardPluginCtx ctx, ShardStr name, int32_t value, ShardStr help, uint32_t flags, ShardCVar* out)
        {
            return CreateCVar<bool>(ctx, name, CVarType::Bool, out, [&](PluginContext& c, const std::string& n) { return c.CVars().Bool(n, value != 0, ToString(help), ToCVarFlags(flags)); });
        }
        ShardResult SHARD_CALL CVarCreateInt(ShardPluginCtx ctx, ShardStr name, int32_t value, ShardStr help, uint32_t flags, int32_t min, int32_t max, ShardCVar* out)
        {
            if (min > max) return SHARD_E_INVALID_ARG;
            return CreateCVar<int>(ctx, name, CVarType::Int, out, [&](PluginContext& c, const std::string& n) { return c.CVars().Int(n, value, ToString(help), ToCVarFlags(flags), min, max); });
        }
        ShardResult SHARD_CALL CVarCreateFloat(ShardPluginCtx ctx, ShardStr name, float value, ShardStr help, uint32_t flags, float min, float max, ShardCVar* out)
        {
            if (!(min <= max)) return SHARD_E_INVALID_ARG;
            return CreateCVar<float>(ctx, name, CVarType::Float, out, [&](PluginContext& c, const std::string& n) { return c.CVars().Float(n, value, ToString(help), ToCVarFlags(flags), min, max); });
        }
        ShardResult SHARD_CALL CVarCreateString(ShardPluginCtx ctx, ShardStr name, ShardStr value, ShardStr help, uint32_t flags, ShardCVar* out)
        {
            return CreateCVar<std::string>(ctx, name, CVarType::String, out, [&](PluginContext& c, const std::string& n) { return c.CVars().String(n, ToString(value), ToString(help), ToCVarFlags(flags)); });
        }

        template <typename T, typename Out>
        ShardResult GetCVar(ShardPluginCtx owner, ShardCVar handle, CVarType type, Out* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                ShardResult result = SHARD_OK;
                Core::CVar<T>* cvar = FindCVar<T>(owner, handle, type, result);
                if (!cvar) return result;
                *out = static_cast<Out>(cvar->Get());
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        template <typename T>
        ShardResult SetCVar(ShardPluginCtx owner, ShardCVar handle, CVarType type, const T& value)
        {
            try {
                ShardResult result = SHARD_OK;
                Core::CVar<T>* cvar = FindCVar<T>(owner, handle, type, result);
                if (!cvar) return result;
                cvar->ForceSet(value);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL CVarGetBool(ShardPluginCtx ctx, ShardCVar cvar, int32_t* out) { return GetCVar<bool>(ctx, cvar, CVarType::Bool, out); }
        ShardResult SHARD_CALL CVarGetInt(ShardPluginCtx ctx, ShardCVar cvar, int32_t* out) { return GetCVar<int>(ctx, cvar, CVarType::Int, out); }
        ShardResult SHARD_CALL CVarGetFloat(ShardPluginCtx ctx, ShardCVar cvar, float* out) { return GetCVar<float>(ctx, cvar, CVarType::Float, out); }
        ShardResult SHARD_CALL CVarGetString(ShardPluginCtx ctx, ShardCVar cvar, char* buffer, uint64_t capacity, uint64_t* size)
        {
            try {
                if (!size || (!buffer && capacity != 0)) return SHARD_E_INVALID_ARG;
                std::string value;
                const ShardResult result = GetCVar<std::string>(ctx, cvar, CVarType::String, &value);
                if (result != SHARD_OK) return result;
                *size = value.size();
                if (buffer && capacity) std::memcpy(buffer, value.data(), size_t(std::min<uint64_t>(capacity, value.size())));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
        ShardResult SHARD_CALL CVarSetBool(ShardPluginCtx ctx, ShardCVar cvar, int32_t value) { return SetCVar<bool>(ctx, cvar, CVarType::Bool, value != 0); }
        ShardResult SHARD_CALL CVarSetInt(ShardPluginCtx ctx, ShardCVar cvar, int32_t value) { return SetCVar<int>(ctx, cvar, CVarType::Int, int(value)); }
        ShardResult SHARD_CALL CVarSetFloat(ShardPluginCtx ctx, ShardCVar cvar, float value) { return SetCVar<float>(ctx, cvar, CVarType::Float, value); }
        ShardResult SHARD_CALL CVarSetString(ShardPluginCtx ctx, ShardCVar cvar, ShardStr value)
        {
            try { return SetCVar<std::string>(ctx, cvar, CVarType::String, ToString(value)); } catch (...) { return SHARD_E_INTERNAL; }
        }

        // ---- shard.commands ----

        ShardResult SHARD_CALL CommandAdd(ShardPluginCtx handle, ShardStr name, ShardStr help, ShardCommandFn run, void* user)
        {
            try {
                if (!run || !name.data || name.size == 0) return SHARD_E_INVALID_ARG;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                {
                    std::string key = ToString(name);
                    for (char& c : key) c = char(std::tolower((unsigned char)c));
                    std::lock_guard<std::mutex> lock(gContextMutex);
                    std::vector<std::string>& names = gCommandNames[handle];
                    if (std::find(names.begin(), names.end(), key) != names.end()) return SHARD_E_ALREADY_EXISTS;
                    names.push_back(std::move(key));
                }
                const bool added = ctx->Commands().Add(ToString(name), ToString(help), [run, user, handle](const std::vector<std::string>& args) {
                    Core::CommandResult result;
                    try {
                        std::vector<ShardStr> words;
                        words.reserve(args.size());
                        for (const std::string& a : args) words.push_back(ToStr(a));
                        ShardStr output{ nullptr, 0 };
                        const ShardResult code = run(user, handle, words.data(), uint32_t(words.size()), &output);
                        result.ok = code == SHARD_OK;
                        result.output = ToString(output);
                        if (!result.ok && result.output.empty()) result.output = "failed (" + std::to_string(code) + ")";
                    } catch (...) {
                        result.ok = false;
                        result.output = "the command threw";
                    }
                    return result;
                });
                return added ? SHARD_OK : SHARD_E_ALREADY_EXISTS;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL HostQueryInterface(ShardStr id, uint32_t major, uint32_t minMinor, const void** out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = nullptr;
                static const ShardPluginApi pluginApi = { sizeof(ShardPluginApi), &PluginId, &PluginVersion, &PluginDirectory };
                if (ToString(id) == SHARD_IFACE_PLUGIN) {
                    if (major != 1 || minMinor > 0) return SHARD_E_NOT_FOUND;
                    *out = &pluginApi;
                    return SHARD_OK;
                }
                if (ToString(id) == SHARD_IFACE_COMMANDS) {
                    static const ShardCommandsApi commandsApi = { sizeof(ShardCommandsApi), &CommandAdd };
                    if (major != 1 || minMinor > 0) return SHARD_E_NOT_FOUND;
                    *out = &commandsApi;
                    return SHARD_OK;
                }
                if (ToString(id) == SHARD_IFACE_CVARS) {
                    static const ShardCVarsApi cvarsApi = {
                        sizeof(ShardCVarsApi), &CVarCreateBool, &CVarCreateInt, &CVarCreateFloat, &CVarCreateString,
                        &CVarGetBool, &CVarGetInt, &CVarGetFloat, &CVarGetString, &CVarSetBool, &CVarSetInt, &CVarSetFloat, &CVarSetString };
                    if (major != 1 || minMinor > 0) return SHARD_E_NOT_FOUND;
                    *out = &cvarsApi;
                    return SHARD_OK;
                }
                return SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // ---- The adapter ----

        class CAbiPlugin final : public IModule {
        public:
            CAbiPlugin(PluginOwner owner, const ShardPluginInfo& info) : m_Owner(owner), m_User(info.user)
            {
                std::memset(&m_Callbacks, 0, sizeof(m_Callbacks));
                if (info.callbacks) std::memcpy(&m_Callbacks, info.callbacks, std::min<size_t>(info.callbacks->structSize, sizeof(m_Callbacks)));
            }

            void OnLoad(PluginContext& ctx) override
            {
                BindContext(m_Owner, &ctx);
                Call("on_load", m_Callbacks.on_load);
            }
            void OnInitialize(PluginContext&) override { Call("on_initialize", m_Callbacks.on_initialize); }
            void OnEnable(PluginContext&) override { Call("on_enable", m_Callbacks.on_enable); }
            void OnDisable(PluginContext&) override
            {
                // The manager destroys the context right after this : the handle must not outlive it (OnLoad binds the new one on enable)
                try { Call("on_disable", m_Callbacks.on_disable); } catch (...) { BindContext(m_Owner, nullptr); throw; }
                BindContext(m_Owner, nullptr);
            }
            void OnUnload(PluginContext&) override
            {
                try { Call("on_unload", m_Callbacks.on_unload); } catch (...) { BindContext(m_Owner, nullptr); throw; }
                BindContext(m_Owner, nullptr);
            }

        private:
            using Callback = ShardResult (SHARD_CALL *)(void*, ShardPluginCtx);

            // A callback is only offered a structSize-checked slot : one that is not there is NULL (zeroed in the constructor)
            void Call(const char* what, Callback fn)
            {
                if (!fn) return;
                const ShardResult result = fn(m_User, m_Owner);
                if (result != SHARD_OK) throw std::runtime_error(std::string(what) + " returned " + std::to_string(result));
            }

            PluginOwner m_Owner;
            void* m_User;
            ShardPluginCallbacks m_Callbacks;
        };
    }

    const ShardHostApi* GetCHostApi()
    {
        static const ShardHostApi api = [] {
            ShardHostApi a;
            std::memset(&a, 0, sizeof(a));
            a.structSize = sizeof(ShardHostApi);
            a.abiMajor = SHARD_ABI_MAJOR;
            a.abiMinor = SHARD_ABI_MINOR;
            a.log = &HostLog;
            a.alloc = &HostAlloc;
            a.free = &HostFree;
            a.query_interface = &HostQueryInterface;
            return a;
        }();
        return &api;
    }

    std::string ValidateCPluginInfo(const ShardPluginInfo& info)
    {
        if (info.structSize < offsetof(ShardPluginInfo, callbacks) + sizeof(info.callbacks))
            return "its ShardPluginInfo is too small (" + std::to_string(info.structSize) + " bytes, at least " +
                   std::to_string(offsetof(ShardPluginInfo, callbacks) + sizeof(info.callbacks)) + " expected)";
        if (info.abiMajor != SHARD_ABI_MAJOR || info.abiMinor > SHARD_ABI_MINOR)
            return "built for the C ABI " + std::to_string(info.abiMajor) + "." + std::to_string(info.abiMinor) + ", this engine has " +
                   std::to_string(SHARD_ABI_MAJOR) + "." + std::to_string(SHARD_ABI_MINOR);
        if (!info.callbacks) return "its ShardPluginInfo has no callbacks table";
        if (info.callbacks->structSize < sizeof(uint32_t)) return "its callbacks table is malformed (structSize " + std::to_string(info.callbacks->structSize) + ")";
        return {};
    }

    IPlugin* CreateCPlugin(PluginOwner owner, const ShardPluginInfo& info) { return new CAbiPlugin(owner, info); }
}

#include "engine/plugins/abi/c_plugin.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

#include <shard/shard_commands.h>
#include <shard/shard_components.h>
#include <shard/shard_content.h>
#include <shard/shard_cvars.h>
#include <shard/shard_ecs.h>
#include <shard/shard_events.h>
#include <shard/shard_experimental.h>
#include <shard/shard_extensions.h>
#include <shard/shard_hooks.h>
#include <shard/shard_plugin.h>
#include <shard/shard_services.h>
#include <shard/shard_state.h>
#include <shard/shard_systems.h>

#include "engine/core/config/cvar.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/world/world.hpp"

#include "config.h"

namespace Shard::Engine::Plugins {

    namespace CAbi {
        namespace {
            // ShardPluginCtx is the plugin's owner id ; the context is looked up for each call, so a handle that outlived the plugin finds nothing
            std::mutex gContextMutex;
            struct BoundContext {
                PluginContext* ctx = nullptr;
                std::string version;        // the "shard.plugin" interface hands out pointers into this, so it lives as long as the binding
                bool experimental = true;   // may use "shard.experimental.*"
            };
            std::unordered_map<ShardPluginCtx, BoundContext> gContexts;

            std::mutex gThreadMutex;
            bool gHasMainThread = false;
            std::thread::id gMainThread;
        }

        PluginContext* FindContext(ShardPluginCtx handle)
        {
            std::lock_guard<std::mutex> lock(gContextMutex);
            auto it = gContexts.find(handle);
            return it == gContexts.end() ? nullptr : it->second.ctx;
        }

        bool ExperimentalAllowed(ShardPluginCtx handle)
        {
            std::lock_guard<std::mutex> lock(gContextMutex);
            auto it = gContexts.find(handle);
            return it != gContexts.end() && it->second.experimental;
        }

        ShardResult CopyOut(const std::string& text, char* buffer, uint64_t capacity, uint64_t* size)
        {
            if (!size || (!buffer && capacity != 0)) return SHARD_E_INVALID_ARG;
            *size = text.size();
            if (buffer && capacity) std::memcpy(buffer, text.data(), size_t(std::min<uint64_t>(capacity, text.size())));
            return SHARD_OK;
        }

        void NoteMainThread()
        {
            std::lock_guard<std::mutex> lock(gThreadMutex);
            if (gHasMainThread) return;
            gHasMainThread = true;
            gMainThread = std::this_thread::get_id();
        }

        bool OnMainThread()
        {
            if (SystemRun* run = CurrentRun())
                if (!run->mainThread) return false;
            std::lock_guard<std::mutex> lock(gThreadMutex);
            return !gHasMainThread || gMainThread == std::this_thread::get_id();
        }
    }

    using namespace CAbi;

    namespace {
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

        void UnbindContext(PluginOwner owner)
        {
            // What needs the context still : the registry the plugin left open, the memory of its blobs
            if (PluginContext* ctx = FindContext(owner)) {
                CloseWorldEcs(owner);
                ReleaseBlobs(owner, *ctx);
            }
            {
                std::lock_guard<std::mutex> lock(gContextMutex);
                gContexts.erase(owner);
                ReleaseCVarSlots(owner);
                gCommandNames.erase(owner);
            }
            ReleaseComponents(owner);
            ReleaseServices(owner);
        }

        // The binding ends when the context releases everything (disable, unload, failure) : its first record, so its last release, after what
        // the plugin registered (an asset type's resources can still be handed back to the plugin while they go)
        void BindContext(PluginOwner owner, PluginContext& ctx)
        {
            NoteMainThread();
            {
                std::lock_guard<std::mutex> lock(gContextMutex);
                // A plugin that is distributed (native, signed by a trusted publisher) is held to the frozen ABI
                bool experimental = true;
                if (const PluginRecord* record = PluginManager::Get().Find(ctx.Id()))
                    experimental = MayUseExperimental(ctx.Manifest().tier, record->signature.status);
                gContexts[owner] = BoundContext{ &ctx, ctx.Manifest().version.ToString(), experimental };
            }
            ctx.OnRelease("c-abi", [owner]() { UnbindContext(owner); });
        }

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
        ShardStr SHARD_CALL PluginHost(ShardPluginCtx handle)
        {
            PluginContext* c = FindContext(handle);
            if (!c) return ShardStr{ nullptr, 0 };
            const char* name = HostName(c->CurrentHost());      // a static string
            return ShardStr{ name, uint64_t(std::strlen(name)) };
        }
        int32_t SHARD_CALL PluginHasPermission(ShardPluginCtx handle, ShardStr permission)
        {
            try {
                PluginContext* c = FindContext(handle);
                return c && c->Has(ToString(permission)) ? SHARD_TRUE : SHARD_FALSE;
            } catch (...) { return SHARD_FALSE; }
        }

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
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
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
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
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
                return CopyOut(value, buffer, capacity, size);
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
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                {
                    std::string key = ToString(name);
                    for (char& c : key) c = char(std::tolower((unsigned char)c));
                    std::lock_guard<std::mutex> lock(gContextMutex);
                    std::vector<std::string>& names = gCommandNames[handle];
                    if (std::find(names.begin(), names.end(), key) != names.end()) return SHARD_E_ALREADY_EXISTS;
                    names.push_back(std::move(key));
                }
                std::atomic<uint32_t>* active = ActiveCounter(*ctx);
                const bool added = ctx->Commands().Add(ToString(name), ToString(help), [run, user, handle, active](const std::vector<std::string>& args) {
                    ActiveScope scope(active, PluginOwner(handle));
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

        // ---- query_interface ----

        struct Interface {
            const char* id;
            uint32_t major;
            uint32_t minor;
            const void* table;
        };

        const std::vector<Interface>& Interfaces()
        {
            static const ShardPluginApi pluginApi = { sizeof(ShardPluginApi), &PluginId, &PluginVersion, &PluginDirectory, &PluginHost, &PluginHasPermission };
            static const ShardCommandsApi commandsApi = { sizeof(ShardCommandsApi), &CommandAdd };
            static const ShardCVarsApi cvarsApi = {
                sizeof(ShardCVarsApi), &CVarCreateBool, &CVarCreateInt, &CVarCreateFloat, &CVarCreateString,
                &CVarGetBool, &CVarGetInt, &CVarGetFloat, &CVarGetString, &CVarSetBool, &CVarSetInt, &CVarSetFloat, &CVarSetString };
            static const std::vector<Interface> interfaces = {
                { SHARD_IFACE_PLUGIN, 1, 1, &pluginApi },
                { SHARD_IFACE_COMMANDS, 1, 0, &commandsApi },
                { SHARD_IFACE_CVARS, 1, 0, &cvarsApi },
                { SHARD_IFACE_COMPONENTS, 1, 1, ComponentsApi() },
                { SHARD_IFACE_ECS, 1, 0, EcsApi() },
                { SHARD_IFACE_SYSTEMS, 1, 0, SystemsApi() },
                { SHARD_IFACE_EVENTS, 1, 1, EventsApi() },
                { SHARD_IFACE_HOOKS, 1, 0, HooksApi() },
                { SHARD_IFACE_SERVICES, 1, 0, ServicesApi() },
                { SHARD_IFACE_EXTENSIONS, 1, 0, ExtensionsApi() },
                { SHARD_IFACE_CONTENT, 1, 0, ContentApi() },
                { SHARD_IFACE_STATE, 1, 0, StateApi() },
                // No promise : major 0, handed out for its exact minor only (see shard_experimental.h)
                { SHARD_IFACE_EXPERIMENTAL_WORLD_ECS, 0, 1, WorldEcsApi() },
            };
            return interfaces;
        }

        ShardResult SHARD_CALL HostQueryInterface(ShardStr id, uint32_t major, uint32_t minMinor, const void** out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = nullptr;
                const std::string wanted = ToString(id);
                for (const Interface& i : Interfaces()) {
                    if (wanted != i.id) continue;
                    if (major != i.major || minMinor > i.minor) return SHARD_E_NOT_FOUND;
                    if (i.major == 0 && minMinor != i.minor) return SHARD_E_NOT_FOUND;    // experimental : that revision exactly
                    *out = i.table;
                    return SHARD_OK;
                }
                return SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // ---- The adapter ----

        ShardWorldInfo WorldInfo(Worlds::World& world, std::string& name)
        {
            name = world.GetName();
            ShardWorldInfo info;
            info.structSize = sizeof(ShardWorldInfo);
            info.assetId = world.ResolveAssetID();
            info.name = ToStr(name);
            return info;
        }

        class CAbiPlugin final : public IModule {
        public:
            CAbiPlugin(PluginOwner owner, const ShardPluginInfo& info) : m_Owner(owner), m_User(info.user)
            {
                std::memset(&m_Callbacks, 0, sizeof(m_Callbacks));
                if (info.callbacks) std::memcpy(&m_Callbacks, info.callbacks, std::min<size_t>(info.callbacks->structSize, sizeof(m_Callbacks)));
                if (m_Callbacks.on_frame_begin || m_Callbacks.on_frame_end) m_Frames = std::make_unique<Frames>(*this);
                if (m_Callbacks.on_tool_task) m_Tool = std::make_unique<Tool>(*this);
            }

            void OnLoad(PluginContext& ctx) override
            {
                BindContext(m_Owner, ctx);
                Call("on_load", m_Callbacks.on_load);
            }
            void OnInitialize(PluginContext&) override { Call("on_initialize", m_Callbacks.on_initialize); }
            void OnEnable(PluginContext&) override { Call("on_enable", m_Callbacks.on_enable); }
            void OnDisable(PluginContext&) override { Call("on_disable", m_Callbacks.on_disable); }
            void OnUnload(PluginContext&) override { Call("on_unload", m_Callbacks.on_unload); }

            bool OnPrepareReload(StateWriter& out) override
            {
                if (!m_Callbacks.on_prepare_reload) return false;
                const ShardState state = BeginState(&out, nullptr);
                ShardResult result = SHARD_E_INTERNAL;
                try { result = m_Callbacks.on_prepare_reload(m_User, m_Owner, state); } catch (...) { EndState(); throw; }
                EndState();
                return result == SHARD_OK;
            }

            void OnRestore(StateReader& in) override
            {
                if (!m_Callbacks.on_restore) return;
                const ShardState state = BeginState(nullptr, &in);
                ShardResult result = SHARD_E_INTERNAL;
                try { result = m_Callbacks.on_restore(m_User, m_Owner, state); } catch (...) { EndState(); throw; }
                EndState();
                Check("on_restore", result);
            }

            void OnContentMounted(PluginContext&) override { Call("on_content_mounted", m_Callbacks.on_content_mounted); }
            void OnWorldLoaded(Worlds::World& world) override { CallWorld("on_world_loaded", m_Callbacks.on_world_loaded, world); }
            void OnWorldUnloaded(Worlds::World& world) override { CallWorld("on_world_unloaded", m_Callbacks.on_world_unloaded, world); }
            void OnPlayStart(Worlds::World& world) override { CallWorld("on_play_start", m_Callbacks.on_play_start, world); }
            void OnPlayStop(Worlds::World& world) override { CallWorld("on_play_stop", m_Callbacks.on_play_stop, world); }

            CPluginFacets Facets() { return CPluginFacets{ m_Frames.get(), m_Tool.get() }; }

        private:
            using Callback = ShardResult (SHARD_CALL *)(void*, ShardPluginCtx);
            using WorldCallback = ShardResult (SHARD_CALL *)(void*, ShardPluginCtx, const ShardWorldInfo*);

            static void Check(const char* what, ShardResult result)
            {
                if (result != SHARD_OK) throw std::runtime_error(std::string(what) + " returned " + std::to_string(result));
            }

            // A callback is only offered a structSize-checked slot : one that is not there is NULL (zeroed in the constructor)
            void Call(const char* what, Callback fn)
            {
                if (fn) Check(what, fn(m_User, m_Owner));
            }

            void CallWorld(const char* what, WorldCallback fn, Worlds::World& world)
            {
                if (!fn) return;
                std::string name;
                const ShardWorldInfo info = WorldInfo(world, name);
                Check(what, fn(m_User, m_Owner, &info));
            }

            class Frames final : public EnginePlugin {
            public:
                explicit Frames(CAbiPlugin& plugin) : m_Plugin(plugin) {}
                void OnFrameBegin(uint64_t frame) override { m_Plugin.CallFrame("on_frame_begin", m_Plugin.m_Callbacks.on_frame_begin, frame); }
                void OnFrameEnd(uint64_t frame) override { m_Plugin.CallFrame("on_frame_end", m_Plugin.m_Callbacks.on_frame_end, frame); }
            private:
                CAbiPlugin& m_Plugin;
            };

            class Tool final : public ToolPlugin {
            public:
                explicit Tool(CAbiPlugin& plugin) : m_Plugin(plugin) {}
                bool OnToolTask(const std::string& task, const std::string& project, std::string& message) override
                {
                    ShardStr out{ nullptr, 0 };
                    const ShardResult result = m_Plugin.m_Callbacks.on_tool_task(m_Plugin.m_User, m_Plugin.m_Owner, ToStr(task), ToStr(project), &out);
                    if (result == SHARD_OK) return true;
                    message = ToString(out);
                    if (message.empty()) message = "refused (" + std::to_string(result) + ")";
                    return false;
                }
            private:
                CAbiPlugin& m_Plugin;
            };

            void CallFrame(const char* what, ShardResult (SHARD_CALL *fn)(void*, ShardPluginCtx, uint64_t), uint64_t frame)
            {
                if (fn) Check(what, fn(m_User, m_Owner, frame));
            }

            PluginOwner m_Owner;
            void* m_User;
            ShardPluginCallbacks m_Callbacks;
            std::unique_ptr<Frames> m_Frames;
            std::unique_ptr<Tool> m_Tool;
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
            a.engineVersion = ShardStr{ VERSION, uint64_t(std::strlen(VERSION)) };     // config.h : the engine's project version
            return a;
        }();
        return &api;
    }

    std::string ValidateCPluginInfo(const ShardPluginInfo& info, uint32_t hostMinor)
    {
        if (info.structSize < offsetof(ShardPluginInfo, callbacks) + sizeof(info.callbacks))
            return "its ShardPluginInfo is too small (" + std::to_string(info.structSize) + " bytes, at least " +
                   std::to_string(offsetof(ShardPluginInfo, callbacks) + sizeof(info.callbacks)) + " expected)";
        if (info.abiMajor != SHARD_ABI_MAJOR || info.abiMinor > hostMinor)
            return "built for the C ABI " + std::to_string(info.abiMajor) + "." + std::to_string(info.abiMinor) + ", this engine has " +
                   std::to_string(SHARD_ABI_MAJOR) + "." + std::to_string(hostMinor);
        if (!info.callbacks) return "its ShardPluginInfo has no callbacks table";
        if (info.callbacks->structSize < sizeof(uint32_t)) return "its callbacks table is malformed (structSize " + std::to_string(info.callbacks->structSize) + ")";
        return {};
    }

    IPlugin* CreateCPlugin(PluginOwner owner, const ShardPluginInfo& info) { return new CAbiPlugin(owner, info); }

    bool MayUseExperimental(TrustTier tier, SignatureInfo::Status status)
    {
        return !(tier == TrustTier::Native && status == SignatureInfo::Status::Valid);
    }

    CPluginFacets GetCPluginFacets(IPlugin* plugin)
    {
        auto* c = dynamic_cast<CAbiPlugin*>(plugin);
        return c ? c->Facets() : CPluginFacets{};
    }
}

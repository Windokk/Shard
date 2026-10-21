// Shard C++ SDK : a plugin class over the C ABI.
//
//   class SpinPlugin : public shard::Plugin {
//   public:
//       void OnLoad(shard::Context& ctx) override { ... }
//   };
//   SHARD_C_PLUGIN(SpinPlugin)
//
// The macro exports shard_plugin_entry and fills the callbacks table : every callback catches what the plugin throws (shard::Error from a
// refused registration, or anything else) and turns it into an error the host fails the plugin with, the message in its log. Nothing of the
// engine is included or linked : this compiles against sdk/include alone, with any C++17 compiler.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <shard/shard.h>
#include <shard/cpp/component.hpp>
#include <shard/cpp/content.hpp>
#include <shard/cpp/cvars.hpp>
#include <shard/cpp/detail.hpp>
#include <shard/cpp/events.hpp>
#include <shard/cpp/services.hpp>
#include <shard/cpp/system.hpp>

namespace shard {

    /// What the host knows of the plugin, and every way the plugin reaches it
    class Context {
    public:
        explicit Context(ShardPluginCtx ctx) : m_Ctx(ctx) {}

        ShardPluginCtx Handle() const { return m_Ctx; }

        std::string_view Id() const { const auto* p = Plugin(); return p ? View(p->id(m_Ctx)) : std::string_view(); }
        std::string_view Version() const { const auto* p = Plugin(); return p ? View(p->version(m_Ctx)) : std::string_view(); }
        /// The plugin's folder, absolute
        std::string_view Directory() const { const auto* p = Plugin(); return p ? View(p->directory(m_Ctx)) : std::string_view(); }
        /// "editor", "player", "headless" or "tool" (empty in an engine of ABI 1.0)
        std::string_view HostName() const { const auto* p = Plugin11(); return p ? View(p->host(m_Ctx)) : std::string_view(); }
        /// True if the manifest asks for that permission (false in an engine of ABI 1.0)
        bool HasPermission(std::string_view permission) const { const auto* p = Plugin11(); return p && p->has_permission(m_Ctx, Str(permission)) == SHARD_TRUE; }

        void Log(std::string_view message) const { detail::Host()->log(m_Ctx, SHARD_LOG_INFO, Str(message)); }
        void Warn(std::string_view message) const { detail::Host()->log(m_Ctx, SHARD_LOG_WARNING, Str(message)); }
        void Error(std::string_view message) const { detail::Host()->log(m_Ctx, SHARD_LOG_ERROR, Str(message)); }

        /// Memory counted against the plugin's budget (manifest budgets.memoryMB) : give it back with Free
        void* Alloc(size_t size, size_t align = 16) const { return detail::Host()->alloc(m_Ctx, uint64_t(size), uint64_t(align)); }
        void Free(void* p) const { detail::Host()->free(m_Ctx, p); }

        shard::Components Components() const { return shard::Components(m_Ctx); }
        shard::Systems Systems() const { return shard::Systems(m_Ctx); }
        shard::CVars CVars() const { return shard::CVars(m_Ctx); }
        shard::Commands Commands() const { return shard::Commands(m_Ctx); }
        shard::Events Events() const { return shard::Events(m_Ctx); }
        shard::Hooks Hooks() const { return shard::Hooks(m_Ctx); }
        shard::Services Services() const { return shard::Services(m_Ctx); }
        shard::Content Content() const { return shard::Content(m_Ctx); }
        shard::Extensions Extensions() const { return shard::Extensions(m_Ctx); }

    private:
        static const ShardPluginApi* Plugin() { return detail::Interface<ShardPluginApi>(SHARD_IFACE_PLUGIN); }
        static const ShardPluginApi* Plugin11() { return detail::Interface<ShardPluginApi>(SHARD_IFACE_PLUGIN, 1); }
        ShardPluginCtx m_Ctx;
    };

    /// A world a module is told about
    struct World {
        int32_t assetId = -1;
        std::string_view name;
    };

    /// The base of a plugin written with the SDK : the same calls, in the same order, as the engine's IPlugin (see shard_host.h)
    class Plugin {
    public:
        virtual ~Plugin() = default;

        /// Register everything here (components, systems, cvars, commands, services, hooks...)
        virtual void OnLoad(Context&) {}
        /// Every plugin ran OnLoad : the services of the others can be asked for
        virtual void OnInitialize(Context&) {}
        virtual void OnEnable(Context&) {}
        virtual void OnDisable(Context&) {}
        virtual void OnUnload(Context&) {}

        /// Hot reload : write what must survive and return true ; false (the default) makes the plugin not reloadable
        virtual bool OnPrepareReload(Context&, StateWriter&) { return false; }
        virtual void OnRestore(Context&, StateReader&) {}

        virtual void OnFrameBegin(Context&, uint64_t /*frame*/) {}
        virtual void OnFrameEnd(Context&, uint64_t /*frame*/) {}

        virtual void OnContentMounted(Context&) {}
        virtual void OnWorldLoaded(Context&, const World&) {}
        virtual void OnWorldUnloaded(Context&, const World&) {}
        virtual void OnPlayStart(Context&, const World&) {}
        virtual void OnPlayStop(Context&, const World&) {}

        /// An offline tool is about to run `task` ("cook", "bake", "validate") : return false (and a message) to cancel it
        virtual bool OnToolTask(Context&, std::string_view /*task*/, std::string_view /*project*/, std::string& /*message*/) { return true; }
    };

    namespace detail {

        inline std::unique_ptr<Plugin>& Instance()
        {
            static std::unique_ptr<Plugin> instance;
            return instance;
        }

        template <typename Fn>
        ShardResult Call(ShardPluginCtx ctx, const char* what, Fn&& fn)
        {
            if (!Instance()) return SHARD_E_INTERNAL;
            return Guard(ctx, what, [&] {
                Context c(ctx);
                fn(*Instance(), c);
            });
        }

        inline World ToWorld(const ShardWorldInfo* info) { return info ? World{ info->assetId, View(info->name) } : World{}; }

        inline const ShardPluginCallbacks& Callbacks()
        {
            static const ShardPluginCallbacks callbacks = [] {
                ShardPluginCallbacks c{};
                c.structSize = sizeof(ShardPluginCallbacks);
                c.on_load = [](void*, ShardPluginCtx ctx) -> ShardResult {
                    KeptObjects().clear();      // what the previous context registered is gone with it
                    return Call(ctx, "OnLoad", [](Plugin& p, Context& c) { p.OnLoad(c); });
                };
                c.on_initialize = [](void*, ShardPluginCtx ctx) { return Call(ctx, "OnInitialize", [](Plugin& p, Context& c) { p.OnInitialize(c); }); };
                c.on_enable = [](void*, ShardPluginCtx ctx) { return Call(ctx, "OnEnable", [](Plugin& p, Context& c) { p.OnEnable(c); }); };
                c.on_disable = [](void*, ShardPluginCtx ctx) { return Call(ctx, "OnDisable", [](Plugin& p, Context& c) { p.OnDisable(c); }); };
                c.on_unload = [](void*, ShardPluginCtx ctx) -> ShardResult {
                    const ShardResult r = Call(ctx, "OnUnload", [](Plugin& p, Context& c) { p.OnUnload(c); });
                    Instance().reset();
                    return r;
                };
                c.on_prepare_reload = [](void*, ShardPluginCtx ctx, ShardState state) -> ShardResult {
                    bool reloadable = false;
                    const ShardResult r = Call(ctx, "OnPrepareReload", [&](Plugin& p, Context& c) {
                        StateWriter w(state);
                        reloadable = p.OnPrepareReload(c, w);
                    });
                    return r != SHARD_OK ? r : reloadable ? SHARD_OK : SHARD_E_UNSUPPORTED;
                };
                c.on_restore = [](void*, ShardPluginCtx ctx, ShardState state) {
                    return Call(ctx, "OnRestore", [&](Plugin& p, Context& c) {
                        StateReader r(state);
                        p.OnRestore(c, r);
                    });
                };
                c.on_frame_begin = [](void*, ShardPluginCtx ctx, uint64_t frame) { return Call(ctx, "OnFrameBegin", [&](Plugin& p, Context& c) { p.OnFrameBegin(c, frame); }); };
                c.on_frame_end = [](void*, ShardPluginCtx ctx, uint64_t frame) { return Call(ctx, "OnFrameEnd", [&](Plugin& p, Context& c) { p.OnFrameEnd(c, frame); }); };
                c.on_content_mounted = [](void*, ShardPluginCtx ctx) { return Call(ctx, "OnContentMounted", [](Plugin& p, Context& c) { p.OnContentMounted(c); }); };
                c.on_world_loaded = [](void*, ShardPluginCtx ctx, const ShardWorldInfo* w) { return Call(ctx, "OnWorldLoaded", [&](Plugin& p, Context& c) { p.OnWorldLoaded(c, ToWorld(w)); }); };
                c.on_world_unloaded = [](void*, ShardPluginCtx ctx, const ShardWorldInfo* w) { return Call(ctx, "OnWorldUnloaded", [&](Plugin& p, Context& c) { p.OnWorldUnloaded(c, ToWorld(w)); }); };
                c.on_play_start = [](void*, ShardPluginCtx ctx, const ShardWorldInfo* w) { return Call(ctx, "OnPlayStart", [&](Plugin& p, Context& c) { p.OnPlayStart(c, ToWorld(w)); }); };
                c.on_play_stop = [](void*, ShardPluginCtx ctx, const ShardWorldInfo* w) { return Call(ctx, "OnPlayStop", [&](Plugin& p, Context& c) { p.OnPlayStop(c, ToWorld(w)); }); };
                c.on_tool_task = [](void*, ShardPluginCtx ctx, ShardStr task, ShardStr project, ShardStr* message) -> ShardResult {
                    static std::string kept;            // read by the host once this returns
                    bool ok = true;
                    kept.clear();
                    const ShardResult r = Call(ctx, "OnToolTask", [&](Plugin& p, Context& c) { ok = p.OnToolTask(c, View(task), View(project), kept); });
                    *message = Str(kept);
                    return r != SHARD_OK ? r : ok ? SHARD_OK : SHARD_E_UNSUPPORTED;
                };
                return c;
            }();
            return callbacks;
        }

        template <typename Create>
        ShardResult Entry(const ShardHostApi* host, ShardPluginInfo* out, const char* name, Create&& create) noexcept
        {
            if (!host || !out) return SHARD_E_INVALID_ARG;
            if (host->abiMajor != SHARD_ABI_MAJOR) return SHARD_E_UNSUPPORTED;
            Host() = host;
            try {
                Instance().reset(create());
            } catch (...) {
                return SHARD_E_INTERNAL;
            }
            out->structSize = sizeof(ShardPluginInfo);
            out->abiMajor = SHARD_ABI_MAJOR;
            out->abiMinor = SHARD_ABI_MINOR;
            out->id = Str(name);
            out->user = nullptr;
            out->callbacks = &Callbacks();
            return SHARD_OK;
        }
    }
}

/// Exports the entry point of the C ABI for a class derived from shard::Plugin (once per plugin, in one source file)
#define SHARD_C_PLUGIN(ClassName)                                                                                   \
    extern "C" SHARD_PLUGIN_EXPORT ShardResult SHARD_CALL shard_plugin_entry(const ShardHostApi* host, ShardPluginInfo* out) \
    {                                                                                                               \
        return ::shard::detail::Entry(host, out, #ClassName, []() -> ::shard::Plugin* { return new ClassName(); }); \
    }

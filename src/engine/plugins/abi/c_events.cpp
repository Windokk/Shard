#include <cstring>
#include <map>
#include <mutex>

#include <shard/shard_components.h>
#include <shard/shard_events.h>
#include <shard/shard_hooks.h>

#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/world/event_system.hpp"

// shard.events (the named channel of the EventDispatcher) and shard.hooks (the HookRegistry, by name)

namespace Shard::Engine::Plugins::CAbi {

    // Describe turns a SHARD_FIELD_* into a NamedEventLayout::FieldType by subtracting SHARD_FIELD_F32 : the two lists are in the same order
    static_assert(int(Events::NamedEventLayout::FieldType::F32) == SHARD_FIELD_F32 - SHARD_FIELD_F32, "field order");
    static_assert(int(Events::NamedEventLayout::FieldType::Bool) == SHARD_FIELD_BOOL - SHARD_FIELD_F32, "field order");
    static_assert(int(Events::NamedEventLayout::FieldType::Color) == SHARD_FIELD_COLOR - SHARD_FIELD_F32, "field order");

    namespace {
        // What systems emitted, by system name : delivered in that order at the end of the frame, whatever order the systems finished in
        std::mutex gQueueMutex;
        std::map<std::string, std::vector<QueuedEvent>> gQueued;

        // ---- shard.events ----

        std::function<void(const Events::NamedEvent&)> Forward(PluginContext& ctx, ShardEventFn fn, void* user, ShardPluginCtx handle)
        {
            std::atomic<uint32_t>* active = ActiveCounter(ctx);
            return [fn, user, handle, active](const Events::NamedEvent& e) {
                ActiveScope scope(active, PluginOwner(handle));
                fn(user, handle, ToStr(e.name), e.payload, uint64_t(e.size));
            };
        }

        ShardResult SHARD_CALL Subscribe(ShardPluginCtx handle, ShardStr name, ShardEventFn fn, void* user, ShardSubscription* out)
        {
            try {
                if (!out || !fn || !name.data || name.size == 0) return SHARD_E_INVALID_ARG;
                *out = 0;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                *out = ctx->Events().SubscribeNamed(ToString(name), Forward(*ctx, fn, user, handle));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SubscribeWorld(ShardPluginCtx handle, int32_t worldId, ShardStr name, ShardEventFn fn, void* user, ShardSubscription* out)
        {
            try {
                if (!out || !fn || !name.data || name.size == 0) return SHARD_E_INVALID_ARG;
                *out = 0;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                *out = ctx->Events().SubscribeNamedToWorld(int(worldId), ToString(name), Forward(*ctx, fn, user, handle));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Unsubscribe(ShardPluginCtx handle, ShardSubscription subscription)
        {
            try {
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                return ctx->Events().Unsubscribe(subscription) ? SHARD_OK : SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult EmitTo(ShardPluginCtx handle, bool toWorld, int32_t worldId, ShardStr name, const void* payload, uint64_t size)
        {
            try {
                if (!name.data || name.size == 0 || (size && !payload)) return SHARD_E_INVALID_ARG;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) {
                    // From a system (or a thread of the plugin's) : delivered at the end of the frame
                    QueuedEvent e;
                    e.toWorld = toWorld;
                    e.world = worldId;
                    e.name = ToString(name);
                    if (size) e.payload.assign(static_cast<const unsigned char*>(payload), static_cast<const unsigned char*>(payload) + size);
                    if (SystemRun* run = CurrentRun()) {
                        run->events.push_back(std::move(e));
                    } else {
                        std::vector<QueuedEvent> one;
                        one.push_back(std::move(e));
                        QueueEvents("~" + ctx->Id(), one);
                    }
                    return SHARD_OK;
                }
                const std::string n = ToString(name);
                const bool sent = toWorld ? ctx->Events().EmitNamedToWorld(int(worldId), n, payload, size_t(size)) : ctx->Events().EmitNamed(n, payload, size_t(size));
                return sent ? SHARD_OK : SHARD_E_UNSUPPORTED;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Emit(ShardPluginCtx handle, ShardStr name, const void* payload, uint64_t size) { return EmitTo(handle, false, 0, name, payload, size); }

        ShardResult SHARD_CALL Describe(ShardPluginCtx handle, ShardStr name, uint32_t size, const ShardFieldDesc* fields, uint32_t fieldCount)
        {
            try {
                if (!name.data || name.size == 0 || size == 0 || size > 65536 || (fieldCount && !fields) || fieldCount > SHARD_COMPONENT_MAX_FIELDS) return SHARD_E_INVALID_ARG;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                Events::NamedEventLayout layout;
                layout.size = size;
                for (uint32_t i = 0; i < fieldCount; ++i) {
                    const ShardFieldDesc& f = fields[i];
                    uint32_t fieldSize = 0, align = 0;
                    Events::NamedEventLayout::Field field;
                    field.name = ToString(f.name);
                    if (f.structSize < sizeof(ShardFieldDesc) || !IsFieldName(field.name) || !FieldLayoutOf(f.type, fieldSize, align)) return SHARD_E_INVALID_ARG;
                    if (f.type > SHARD_FIELD_COLOR) return SHARD_E_INVALID_ARG;     // a payload is copied as bytes : a blob, a string (a blob handle) or an asset in it would mean nothing
                    if (uint64_t(f.offset) + fieldSize > size) return SHARD_E_INVALID_ARG;
                    for (const auto& other : layout.fields) if (other.name == field.name) return SHARD_E_INVALID_ARG;
                    field.type = Events::NamedEventLayout::FieldType(f.type - SHARD_FIELD_F32);     // the same order
                    field.offset = f.offset;
                    layout.fields.push_back(std::move(field));
                }
                return ctx->Events().DescribeNamed(ToString(name), std::move(layout)) ? SHARD_OK : SHARD_E_ALREADY_EXISTS;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
        ShardResult SHARD_CALL EmitWorld(ShardPluginCtx handle, int32_t worldId, ShardStr name, const void* payload, uint64_t size) { return EmitTo(handle, true, worldId, name, payload, size); }

        // ---- shard.hooks ----

        // The fields of a hook of Hooks:: that the C struct shows
        struct HookView {
            std::string subject;
            std::string detail;
            uint64_t frame = 0;
        };

        HookView View(const HookBase&) { return {}; }
        HookView View(const Hooks::PreWorldLoad& h) { return { h.worldName, {}, 0 }; }
        HookView View(const Hooks::PostWorldLoad& h) { return { h.worldName, {}, 0 }; }
        HookView View(const Hooks::PreFrame& h) { return { {}, {}, h.frame }; }
        HookView View(const Hooks::PostFrame& h) { return { {}, {}, h.frame }; }
        HookView View(const Hooks::PreSave& h) { return { h.what, {}, 0 }; }
        HookView View(const Hooks::AssetImported& h) { return { h.path, {}, 0 }; }
        HookView View(const Hooks::PlayerConnecting& h) { return { h.clientName, h.address, 0 }; }

        template <typename H>
        HookRegistry::Token On(PluginContext& ctx, ShardPluginCtx handle, int32_t priority, ShardHookFn fn, void* user)
        {
            std::atomic<uint32_t>* active = ActiveCounter(ctx);
            return ctx.Hooks().On<H>(Priority(priority), std::function<void(H&)>([handle, fn, user, active](H& hook) {
                ActiveScope scope(active, PluginOwner(handle));
                const HookView view = View(hook);
                ShardHookArgs args;
                std::memset(&args, 0, sizeof(args));
                args.structSize = sizeof(ShardHookArgs);
                args.cancelled = hook.cancelled ? 1 : 0;
                args.name = ShardStr{ H::kName, uint64_t(std::strlen(H::kName)) };
                args.subject = ToStr(view.subject);
                args.detail = ToStr(view.detail);
                args.frame = view.frame;
                fn(user, handle, &args);
                if (args.cancelled && !hook.cancelled) hook.Cancel(ToString(args.reason));
                else if (!args.cancelled && hook.cancelled) { hook.cancelled = false; hook.reason.clear(); }
                else if (args.cancelled && args.reason.size) hook.reason = ToString(args.reason);
            }));
        }

        ShardResult SHARD_CALL HookOn(ShardPluginCtx handle, ShardStr name, int32_t priority, ShardHookFn fn, void* user, ShardHookToken* out)
        {
            try {
                if (!out || !fn) return SHARD_E_INVALID_ARG;
                *out = 0;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                const std::string n = ToString(name);
                HookRegistry::Token token = 0;
                if (n == Hooks::PreEngineInit::kName) token = On<Hooks::PreEngineInit>(*ctx, handle, priority, fn, user);
                else if (n == Hooks::PostEngineInit::kName) token = On<Hooks::PostEngineInit>(*ctx, handle, priority, fn, user);
                else if (n == Hooks::PreWorldLoad::kName) token = On<Hooks::PreWorldLoad>(*ctx, handle, priority, fn, user);
                else if (n == Hooks::PostWorldLoad::kName) token = On<Hooks::PostWorldLoad>(*ctx, handle, priority, fn, user);
                else if (n == Hooks::PrePlay::kName) token = On<Hooks::PrePlay>(*ctx, handle, priority, fn, user);
                else if (n == Hooks::PostPlay::kName) token = On<Hooks::PostPlay>(*ctx, handle, priority, fn, user);
                else if (n == Hooks::PreFrame::kName) token = On<Hooks::PreFrame>(*ctx, handle, priority, fn, user);
                else if (n == Hooks::PostFrame::kName) token = On<Hooks::PostFrame>(*ctx, handle, priority, fn, user);
                else if (n == Hooks::PreSave::kName) token = On<Hooks::PreSave>(*ctx, handle, priority, fn, user);
                else if (n == Hooks::AssetImported::kName) token = On<Hooks::AssetImported>(*ctx, handle, priority, fn, user);
                else if (n == Hooks::PlayerConnecting::kName) token = On<Hooks::PlayerConnecting>(*ctx, handle, priority, fn, user);
                else if (n == Hooks::PreShutdown::kName) token = On<Hooks::PreShutdown>(*ctx, handle, priority, fn, user);
                else return SHARD_E_NOT_FOUND;
                if (token == 0) return SHARD_E_UNSUPPORTED;     // no hook registry in this host
                *out = token;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL HookOff(ShardPluginCtx handle, ShardHookToken token)
        {
            try {
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                return ctx->Hooks().Off(token) ? SHARD_OK : SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    void QueueEvents(const std::string& systemName, std::vector<QueuedEvent>& events)
    {
        std::lock_guard<std::mutex> lock(gQueueMutex);
        std::vector<QueuedEvent>& queue = gQueued[systemName];
        for (QueuedEvent& e : events) queue.push_back(std::move(e));
        events.clear();
    }

    const void* EventsApi()
    {
        static const ShardEventsApi api = { sizeof(ShardEventsApi), &Subscribe, &SubscribeWorld, &Unsubscribe, &Emit, &EmitWorld, &Describe };
        return &api;
    }

    const void* HooksApi()
    {
        static const ShardHooksApi api = { sizeof(ShardHooksApi), &HookOn, &HookOff };
        return &api;
    }
}

namespace Shard::Engine::Plugins {

    void FlushCEvents(Events::EventDispatcher* dispatcher)
    {
        std::map<std::string, std::vector<CAbi::QueuedEvent>> queued;
        {
            std::lock_guard<std::mutex> lock(CAbi::gQueueMutex);
            queued.swap(CAbi::gQueued);
        }
        if (!dispatcher) return;
        for (auto& [system, events] : queued) {
            for (const CAbi::QueuedEvent& e : events) {
                const void* data = e.payload.empty() ? nullptr : e.payload.data();
                if (e.toWorld) dispatcher->emitNamedToWorld(int(e.world), e.name, data, e.payload.size());
                else dispatcher->emitNamed(e.name, data, e.payload.size());
            }
        }
    }
}

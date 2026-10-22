// Shard C++ SDK : events by name (shard_events.h) and the engine's hooks (shard_hooks.h).
#pragma once

#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

#include <shard/shard_events.h>
#include <shard/shard_hooks.h>
#include <shard/cpp/component.hpp>
#include <shard/cpp/detail.hpp>

namespace Shard {

    using EventFn = std::function<void(std::string_view name, const void* payload, size_t size)>;

    class Events {
    public:
        explicit Events(ShardPluginCtx ctx) : m_Ctx(ctx) {}

        /// Events sent globally under `name` ; the subscription ends with the plugin, or with Unsubscribe
        ShardSubscription Subscribe(const std::string& name, EventFn fn) const
        {
            ShardSubscription token = 0;
            detail::Check(Api().subscribe(m_Ctx, Str(name), &Trampoline, detail::Keep(std::move(fn)), &token), "subscription to '" + name + "'");
            return token;
        }
        ShardSubscription SubscribeWorld(int32_t worldId, const std::string& name, EventFn fn) const
        {
            ShardSubscription token = 0;
            detail::Check(Api().subscribe_world(m_Ctx, worldId, Str(name), &Trampoline, detail::Keep(std::move(fn)), &token), "subscription to '" + name + "'");
            return token;
        }
        /// A payload of type T (trivially copyable) : events of another size are ignored
        template <typename T>
        ShardSubscription Subscribe(const std::string& name, std::function<void(const T&)> fn) const
        {
            static_assert(std::is_trivially_copyable<T>::value, "an event's payload is copied as bytes");
            return Subscribe(name, [fn = std::move(fn)](std::string_view, const void* payload, size_t size) {
                if (size != sizeof(T)) return;
                T value;
                std::memcpy(&value, payload, sizeof(T));
                fn(value);
            });
        }
        bool Unsubscribe(ShardSubscription token) const { return Api().unsubscribe(m_Ctx, token) == SHARD_OK; }

        /// @brief Says what the payload T of the events named `name` holds (its fields, as SHARD_COMPONENT lists them), for the tools that show
        /// events (the console's event.watch). Throws Shard::Error if another plugin described that name, or if the engine is older than ABI 1.2.
        template <typename T>
        void Describe(const std::string& name) const
        {
            static_assert(std::is_trivially_copyable<T>::value, "an event's payload is copied as bytes");
            static_assert(ComponentTraits<T>::kDescribed, "list the fields of the payload with SHARD_COMPONENT");
            const auto* api = detail::Interface<ShardEventsApi>(SHARD_IFACE_EVENTS, 1);
            if (!api) throw Error(SHARD_E_UNSUPPORTED, "describing event '" + name + "' needs shard.events 1.1");
            const T sample{};
            const std::vector<ShardFieldDesc> fields = detail::FieldsOf<T>(sample);
            detail::Check(api->describe(m_Ctx, Str(name), uint32_t(sizeof(T)), fields.data(), uint32_t(fields.size())), "event '" + name + "'");
        }

        /// From a system it is delivered at the end of the frame ; from the main thread, at once
        bool Emit(const std::string& name, const void* payload, size_t size) const { return Api().emit(m_Ctx, Str(name), payload, uint64_t(size)) == SHARD_OK; }
        bool EmitWorld(int32_t worldId, const std::string& name, const void* payload, size_t size) const { return Api().emit_world(m_Ctx, worldId, Str(name), payload, uint64_t(size)) == SHARD_OK; }
        template <typename T>
        bool Emit(const std::string& name, const T& payload) const
        {
            static_assert(std::is_trivially_copyable<T>::value, "an event's payload is copied as bytes");
            return Emit(name, &payload, sizeof(T));
        }

    private:
        static void SHARD_CALL Trampoline(void* user, ShardPluginCtx ctx, ShardStr name, const void* payload, uint64_t size)
        {
            detail::Guard(ctx, "an event handler", [&] { (*static_cast<EventFn*>(user))(View(name), payload, size_t(size)); });
        }
        static const ShardEventsApi& Api() { return detail::Require<ShardEventsApi>(SHARD_IFACE_EVENTS); }
        ShardPluginCtx m_Ctx;
    };

    /// What a hook handler sees (see the table of shard_hooks.h)
    class Hook {
    public:
        explicit Hook(ShardHookArgs& args) : m_Args(args) {}
        std::string_view Name() const { return View(m_Args.name); }
        std::string_view Subject() const { return View(m_Args.subject); }
        std::string_view Detail() const { return View(m_Args.detail); }
        uint64_t Frame() const { return m_Args.frame; }
        bool Cancelled() const { return m_Args.cancelled != 0; }
        void Cancel(std::string reason = {})
        {
            m_Reason = std::move(reason);
            m_Args.cancelled = 1;
            m_Args.reason = Str(m_Reason);      // copied by the host when the handler returns, while this object is alive
        }
        void Uncancel() { m_Args.cancelled = 0; m_Args.reason = ShardStr{ nullptr, 0 }; }

    private:
        ShardHookArgs& m_Args;
        std::string m_Reason;
    };

    using HookFn = std::function<void(Hook&)>;

    class Hooks {
    public:
        explicit Hooks(ShardPluginCtx ctx) : m_Ctx(ctx) {}
        /// `name` : one of the table of shard_hooks.h ("PreSave"...). Throws Shard::Error for an unknown one.
        ShardHookToken On(const std::string& name, HookFn fn, int32_t priority = SHARD_PRIORITY_NORMAL) const
        {
            ShardHookToken token = 0;
            detail::Check(Api().on(m_Ctx, Str(name), priority, &Trampoline, detail::Keep(std::move(fn)), &token), "hook '" + name + "'");
            return token;
        }
        bool Off(ShardHookToken token) const { return Api().off(m_Ctx, token) == SHARD_OK; }

    private:
        static void SHARD_CALL Trampoline(void* user, ShardPluginCtx ctx, ShardHookArgs* args)
        {
            // The host reads the reason once this returns : it has to outlive the Hook
            static thread_local std::string reason;
            {
                Hook hook(*args);
                detail::Guard(ctx, "a hook handler", [&] { (*static_cast<HookFn*>(user))(hook); });
                reason = ToString(args->reason);
            }
            args->reason = Str(reason);
        }
        static const ShardHooksApi& Api() { return detail::Require<ShardHooksApi>(SHARD_IFACE_HOOKS); }
        ShardPluginCtx m_Ctx;
    };
}

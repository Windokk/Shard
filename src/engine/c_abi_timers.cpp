#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <shard/shard_timers.h>

#include "engine/plugins/abi/c_handles.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"

// The timers of a plugin live in the scheduler of the active world (world/scheduler.hpp). What ties them to the plugin is the owner : a token
// that dies with the plugin's context, after which the scheduler drops them without ever calling into the (possibly unloaded) library.

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        struct Latch {
            std::atomic<bool> fired{ false };
            uint64_t subscription = 0;
        };

        // Per plugin : what a plugin that loops over start() or event_latch() can take before it is refused
        constexpr size_t kMaxTimers = 16384;
        constexpr size_t kMaxLatches = 1024;

        OwnedTable<Latch> gLatches;
        std::mutex gMutex;
        std::unordered_map<PluginOwner, std::shared_ptr<int>> gTokens;

        Worlds::Scheduler* ActiveScheduler()
        {
            Core::IEngineContext* engine = Core::gSharedEnginePtr;
            Worlds::WorldManager* worlds = engine ? engine->GetWorldManager() : nullptr;
            // GetWorldAt logs an error for an index that does not exist, which no world loaded is not
            Worlds::World* world = worlds && worlds->GetLoadedWorldCount() > 0 ? worlds->GetWorldAt(0) : nullptr;
            return world ? &world->GetScheduler() : nullptr;
        }

        void ReleaseTimers(PluginOwner owner)
        {
            std::shared_ptr<int> token;
            {
                std::lock_guard<std::mutex> lock(gMutex);
                auto it = gTokens.find(owner);
                if (it == gTokens.end()) return;
                token = std::move(it->second);
                gTokens.erase(it);
            }
            if (Worlds::Scheduler* scheduler = ActiveScheduler()) scheduler->CancelOwned(token.get());
            gLatches.RemoveOwner(owner);
        }

        // Null when the context is gone. The first call hooks the release.
        std::shared_ptr<int> TokenOf(ShardPluginCtx handle, PluginContext*& ctx)
        {
            ctx = FindContext(handle);
            if (!ctx) return nullptr;
            const PluginOwner owner = PluginOwner(handle);
            bool hook = false;
            std::shared_ptr<int> token;
            {
                std::lock_guard<std::mutex> lock(gMutex);
                auto& slot = gTokens[owner];
                if (!slot) { slot = std::make_shared<int>(0); hook = true; }
                token = slot;
            }
            if (hook) ctx->OnRelease("timers", [owner] { ReleaseTimers(owner); });
            return token;
        }

        bool CallerAllowed() { return OnMainThread() || OnScriptThread(); }

        // What every entry point starts with : the plugin's token and the scheduler to put the timer in.
        ShardResult Prepare(ShardPluginCtx handle, PluginContext*& ctx, std::shared_ptr<int>& token, Worlds::Scheduler*& scheduler)
        {
            if (!CallerAllowed()) return SHARD_E_WRONG_THREAD;
            token = TokenOf(handle, ctx);
            if (!token) return SHARD_E_STALE_HANDLE;
            scheduler = ActiveScheduler();
            return scheduler ? SHARD_OK : SHARD_E_UNSUPPORTED;
        }

        // For the calls that add a timer
        ShardResult PrepareToAdd(ShardPluginCtx handle, PluginContext*& ctx, std::shared_ptr<int>& token, Worlds::Scheduler*& scheduler)
        {
            if (const ShardResult r = Prepare(handle, ctx, token, scheduler); r != SHARD_OK) return r;
            if (Worlds::Scheduler::Refusing()) return SHARD_E_UNSUPPORTED;
            return scheduler->CountOwned(token.get()) >= kMaxTimers ? SHARD_E_OUT_OF_MEMORY : SHARD_OK;
        }

        Worlds::Scheduler::Callback Wrap(PluginContext& ctx, ShardPluginCtx handle, ShardTimerFn fn, void* user)
        {
            std::atomic<uint32_t>* active = ActiveCounter(ctx);
            return [active, handle, fn, user] {
                ActiveScope scope(active, PluginOwner(handle));
                ScriptScope script;
                fn(user);
            };
        }

        ShardResult SHARD_CALL After(ShardPluginCtx handle, double seconds, ShardTimerFn fn, void* user, ShardTimer* out)
        {
            try {
                if (!out || !fn || !std::isfinite(seconds)) return SHARD_E_INVALID_ARG;
                *out = 0;
                PluginContext* ctx = nullptr;
                std::shared_ptr<int> token;
                Worlds::Scheduler* scheduler = nullptr;
                if (const ShardResult r = PrepareToAdd(handle, ctx, token, scheduler); r != SHARD_OK) return r;
                *out = scheduler->After(seconds, Wrap(*ctx, handle, fn, user), token);
                return *out ? SHARD_OK : SHARD_E_INVALID_ARG;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Every(ShardPluginCtx handle, double interval, ShardTimerFn fn, void* user, ShardTimer* out)
        {
            try {
                if (!out || !fn || !std::isfinite(interval)) return SHARD_E_INVALID_ARG;
                *out = 0;
                PluginContext* ctx = nullptr;
                std::shared_ptr<int> token;
                Worlds::Scheduler* scheduler = nullptr;
                if (const ShardResult r = PrepareToAdd(handle, ctx, token, scheduler); r != SHARD_OK) return r;
                *out = scheduler->Every(interval, Wrap(*ctx, handle, fn, user), token);
                return *out ? SHARD_OK : SHARD_E_INVALID_ARG;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Start(ShardPluginCtx handle, ShardStepFn step, void* user, ShardTimer* out)
        {
            try {
                if (!out || !step) return SHARD_E_INVALID_ARG;
                *out = 0;
                PluginContext* ctx = nullptr;
                std::shared_ptr<int> token;
                Worlds::Scheduler* scheduler = nullptr;
                if (const ShardResult r = Prepare(handle, ctx, token, scheduler); r != SHARD_OK) return r;
                std::atomic<uint32_t>* active = ActiveCounter(*ctx);
                const PluginOwner owner = PluginOwner(handle);
                *out = scheduler->Start([active, owner, step, user]() -> Worlds::Wait {
                    ShardWait wait{};
                    wait.kind = SHARD_WAIT_DONE;
                    {
                        ActiveScope scope(active, owner);
                        ScriptScope script;
                        step(user, &wait);
                    }
                    switch (wait.kind) {
                        case SHARD_WAIT_SECONDS:
                            return Worlds::Wait::Seconds(wait.seconds);
                        case SHARD_WAIT_LATCH: {
                            std::shared_ptr<Latch> latch = gLatches.Get(owner, wait.latch);
                            if (!latch) return Worlds::Wait::Done();
                            const double timeout = std::isfinite(wait.seconds) && wait.seconds >= 0.0 ? wait.seconds : std::numeric_limits<double>::infinity();
                            return Worlds::Wait::Until([latch] { return latch->fired.load(); }, timeout);
                        }
                        default:
                            return Worlds::Wait::Done();
                    }
                }, token);
                return *out ? SHARD_OK : SHARD_E_INVALID_ARG;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Cancel(ShardPluginCtx handle, ShardTimer timer)
        {
            try {
                PluginContext* ctx = nullptr;
                std::shared_ptr<int> token;
                Worlds::Scheduler* scheduler = nullptr;
                if (const ShardResult r = Prepare(handle, ctx, token, scheduler); r != SHARD_OK) return r == SHARD_E_UNSUPPORTED ? SHARD_E_NOT_FOUND : r;
                return scheduler->Cancel(timer, token.get()) ? SHARD_OK : SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL CancelAll(ShardPluginCtx handle)
        {
            try {
                PluginContext* ctx = nullptr;
                std::shared_ptr<int> token;
                Worlds::Scheduler* scheduler = nullptr;
                const ShardResult r = Prepare(handle, ctx, token, scheduler);
                if (r == SHARD_OK) scheduler->CancelOwned(token.get());
                return r == SHARD_E_UNSUPPORTED ? SHARD_OK : r;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        int32_t SHARD_CALL IsActive(ShardPluginCtx handle, ShardTimer timer)
        {
            try {
                PluginContext* ctx = nullptr;
                std::shared_ptr<int> token;
                Worlds::Scheduler* scheduler = nullptr;
                if (Prepare(handle, ctx, token, scheduler) != SHARD_OK) return SHARD_FALSE;
                return scheduler->IsActive(timer, token.get()) ? SHARD_TRUE : SHARD_FALSE;
            } catch (...) { return SHARD_FALSE; }
        }

        ShardResult SHARD_CALL EventLatch(ShardPluginCtx handle, ShardStr name, ShardLatch* out)
        {
            try {
                if (!out || !name.data || name.size == 0) return SHARD_E_INVALID_ARG;
                *out = 0;
                if (!CallerAllowed()) return SHARD_E_WRONG_THREAD;
                PluginContext* ctx = nullptr;
                if (!TokenOf(handle, ctx)) return SHARD_E_STALE_HANDLE;
                if (gLatches.CountOf(PluginOwner(handle)) >= kMaxLatches) return SHARD_E_OUT_OF_MEMORY;
                auto latch = std::make_shared<Latch>();
                latch->subscription = ctx->Events().SubscribeNamed(ToString(name), [latch](const Events::NamedEvent&) { latch->fired = true; });
                *out = gLatches.Add(PluginOwner(handle), latch);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        int32_t SHARD_CALL LatchFired(ShardPluginCtx handle, ShardLatch latch)
        {
            try {
                const std::shared_ptr<Latch> l = gLatches.Get(PluginOwner(handle), latch);
                return l && l->fired ? SHARD_TRUE : SHARD_FALSE;
            } catch (...) { return SHARD_FALSE; }
        }

        ShardResult SHARD_CALL LatchRelease(ShardPluginCtx handle, ShardLatch latch)
        {
            try {
                if (!CallerAllowed()) return SHARD_E_WRONG_THREAD;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                std::shared_ptr<Latch> removed;
                if (!gLatches.Remove(PluginOwner(handle), latch, &removed)) return SHARD_E_NOT_FOUND;
                ctx->Events().Unsubscribe(removed->subscription);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    const void* TimersApi()
    {
        static const ShardTimersApi api = { sizeof(ShardTimersApi), &After, &Every, &Start, &Cancel, &CancelAll, &IsActive, &EventLatch, &LatchFired, &LatchRelease };
        return &api;
    }
}

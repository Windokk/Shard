#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <shard/shard_jobs.h>

#include "engine/core/jobs/job_system.hpp"
#include "engine/core/jobs/parallel_for.hpp"
#include "engine/plugins/abi/c_handles.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/world/engine.hpp"

// shard.jobs : the engine's job system for a plugin's own work. The jobs of a plugin that have no handle are all part of one group per plugin, the
// others have a group of their own : disabling the plugin waits for every one of them (the host never calls into a library that is about to go).

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        struct JobEntry {
            Core::JobGroup group;
            std::atomic<bool> released{ false };        // the plugin let go of the handle ; the entry stays until its work is done
        };

        struct PluginJobs {
            std::mutex mutex;
            std::deque<std::pair<ShardMainFn, void*>> posts;
            Core::JobGroup all;                         // every job of the plugin that has no handle of its own
            bool releaseHooked = false;
        };

        OwnedTable<JobEntry> gJobs;
        std::mutex gPluginsMutex;
        std::unordered_map<PluginOwner, std::shared_ptr<PluginJobs>> gPlugins;

        Core::JobSystem* Jobs()
        {
            Core::IEngineContext* engine = Core::gSharedEnginePtr;
            return engine ? engine->GetJobSystem() : nullptr;
        }

        // The thread that waits (the main thread) is 0 ; the pool's threads get 1.. in the order they first run a job of a plugin
        uint32_t WorkerIndex()
        {
            thread_local uint32_t cached = ~0u;
            if (cached != ~0u) return cached;
            static std::mutex mutex;
            static std::unordered_map<std::thread::id, uint32_t> indices;
            std::lock_guard<std::mutex> lock(mutex);
            if (IsMainThreadId(std::this_thread::get_id())) return cached = 0;
            auto it = indices.find(std::this_thread::get_id());
            if (it == indices.end()) {
                Core::JobSystem* js = Jobs();
                const uint32_t count = js ? js->ThreadCount() : 1;
                it = indices.emplace(std::this_thread::get_id(), 1 + uint32_t(indices.size()) % (count > 1 ? count - 1 : 1)).first;
            }
            return cached = it->second;
        }

        std::shared_ptr<PluginJobs> StateOf(ShardPluginCtx handle)
        {
            PluginContext* ctx = FindContext(handle);
            if (!ctx) return nullptr;
            std::shared_ptr<PluginJobs> state;
            bool hook = false;
            {
                std::lock_guard<std::mutex> lock(gPluginsMutex);
                auto& slot = gPlugins[PluginOwner(handle)];
                if (!slot) slot = std::make_shared<PluginJobs>();
                state = slot;
                if (!state->releaseHooked) { state->releaseHooked = true; hook = true; }
            }
            if (hook) {
                const PluginOwner owner = PluginOwner(handle);
                // Waits for the plugin's jobs while its library is still loaded, and drops its pending posts
                ctx->OnRelease("jobs", [owner, state] {
                    if (Core::JobSystem* js = Jobs()) {
                        js->Wait(state->all);
                        for (auto& entry : gJobs.RemoveOwner(owner)) js->Wait(entry->group);
                    } else {
                        gJobs.RemoveOwner(owner);
                    }
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        state->posts.clear();
                    }
                    std::lock_guard<std::mutex> lock(gPluginsMutex);
                    gPlugins.erase(owner);
                });
            }
            return state;
        }

        // Handles the plugin let go of whose work ended
        void Prune(PluginOwner owner)
        {
            std::vector<uint64_t> done;
            gJobs.ForEach(owner, [&](uint64_t handle, const JobEntry& entry) {
                if (entry.released.load() && entry.group.IsDone()) done.push_back(handle);
            });
            for (uint64_t handle : done) gJobs.Remove(owner, handle);
        }

        uint32_t SHARD_CALL WorkerCount()
        {
            Core::JobSystem* js = Jobs();
            return js ? js->ThreadCount() : 1;
        }

        ShardResult SHARD_CALL Run(ShardPluginCtx ctx, ShardJobFn fn, void* user, ShardJob* out)
        {
            try {
                if (!fn) return SHARD_E_INVALID_ARG;
                PluginContext* plugin = FindContext(ctx);
                std::shared_ptr<PluginJobs> state = StateOf(ctx);
                if (!plugin || !state) return SHARD_E_STALE_HANDLE;
                Core::JobSystem* js = Jobs();
                std::atomic<uint32_t>* active = ActiveCounter(*plugin);
                const PluginOwner owner = PluginOwner(ctx);
                auto body = [active, owner, fn, user] {
                    ActiveScope scope(active, owner);
                    try { fn(user, WorkerIndex()); } catch (...) {}
                };
                if (!js) { body(); if (out) *out = 0; return SHARD_OK; }     // no job system (a tool) : run it here, now
                Prune(owner);
                if (out) {
                    auto entry = std::make_shared<JobEntry>();
                    *out = gJobs.Add(owner, entry);
                    js->Submit(entry->group, body);
                } else {
                    js->Submit(state->all, body);
                }
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult ParallelFor(ShardPluginCtx ctx, uint32_t count, uint32_t grain, ShardRangeFn fn, void* user, ShardJob* async)
        {
            try {
                if (!fn) return SHARD_E_INVALID_ARG;
                PluginContext* plugin = FindContext(ctx);
                if (!plugin) return SHARD_E_STALE_HANDLE;
                if (count == 0) { if (async) *async = 0; return SHARD_OK; }
                Core::JobSystem* js = Jobs();
                std::atomic<uint32_t>* active = ActiveCounter(*plugin);
                const PluginOwner owner = PluginOwner(ctx);
                if (!js) {
                    ActiveScope scope(active, owner);
                    fn(user, 0, count, 0);
                    if (async) *async = 0;
                    return SHARD_OK;
                }
                const size_t g = std::max<size_t>(1, grain ? grain : js->DefaultGrain(count));
                if (!async) {
                    Core::ParallelForRange(*js, 0, count, g, [&](size_t b, size_t e) {
                        ActiveScope scope(active, owner);
                        try { fn(user, uint32_t(b), uint32_t(e), WorkerIndex()); } catch (...) {}
                    });
                    return SHARD_OK;
                }
                // The slices are separate jobs of the handle's group
                StateOf(ctx);
                Prune(owner);
                auto entry = std::make_shared<JobEntry>();
                *async = gJobs.Add(owner, entry);
                for (size_t b = 0; b < count; b += g) {
                    const size_t e = std::min<size_t>(count, b + g);
                    js->Submit(entry->group, [active, owner, fn, user, b, e] {
                        ActiveScope scope(active, owner);
                        try { fn(user, uint32_t(b), uint32_t(e), WorkerIndex()); } catch (...) {}
                    });
                }
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ParallelForSync(ShardPluginCtx ctx, uint32_t count, uint32_t grain, ShardRangeFn fn, void* user)
        {
            if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
            return ParallelFor(ctx, count, grain, fn, user, nullptr);
        }
        ShardResult SHARD_CALL ParallelForAsync(ShardPluginCtx ctx, uint32_t count, uint32_t grain, ShardRangeFn fn, void* user, ShardJob* out)
        {
            if (!out) return SHARD_E_INVALID_ARG;
            return ParallelFor(ctx, count, grain, fn, user, out);
        }

        int32_t SHARD_CALL IsDone(ShardPluginCtx ctx, ShardJob job)
        {
            if (!job) return SHARD_TRUE;                // a job that ran inline, or had no work
            std::shared_ptr<JobEntry> entry = gJobs.Get(PluginOwner(ctx), job);
            return !entry || entry->group.IsDone() ? SHARD_TRUE : SHARD_FALSE;
        }

        ShardResult SHARD_CALL Wait(ShardPluginCtx ctx, ShardJob job)
        {
            try {
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                if (!job) return SHARD_OK;
                std::shared_ptr<JobEntry> entry = gJobs.Get(PluginOwner(ctx), job);
                if (!entry || entry->released.load()) return SHARD_E_STALE_HANDLE;
                if (Core::JobSystem* js = Jobs()) js->Wait(entry->group);
                gJobs.Remove(PluginOwner(ctx), job);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Release(ShardPluginCtx ctx, ShardJob job)
        {
            if (!job) return SHARD_OK;
            std::shared_ptr<JobEntry> entry = gJobs.Get(PluginOwner(ctx), job);
            if (!entry || entry->released.exchange(true)) return SHARD_E_STALE_HANDLE;
            // Its work may still run and the group must outlive it : the entry stays in the table until it is done (Prune), or the plugin goes
            if (entry->group.IsDone()) gJobs.Remove(PluginOwner(ctx), job);
            return SHARD_OK;
        }

        ShardResult SHARD_CALL PostMain(ShardPluginCtx ctx, ShardMainFn fn, void* user)
        {
            try {
                if (!fn) return SHARD_E_INVALID_ARG;
                std::shared_ptr<PluginJobs> state = StateOf(ctx);
                if (!state) return SHARD_E_STALE_HANDLE;
                std::lock_guard<std::mutex> lock(state->mutex);
                state->posts.emplace_back(fn, user);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    void RunMainPosts()
    {
        std::vector<std::pair<PluginOwner, std::shared_ptr<PluginJobs>>> plugins;
        {
            std::lock_guard<std::mutex> lock(gPluginsMutex);
            for (auto& [owner, state] : gPlugins) plugins.emplace_back(owner, state);
        }
        // In the order the plugins were loaded (their owner ids), so a replay runs the posts in the same order
        std::sort(plugins.begin(), plugins.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& [owner, state] : plugins) {
            std::deque<std::pair<ShardMainFn, void*>> posts;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                posts.swap(state->posts);
            }
            PluginContext* ctx = FindContext(owner);
            if (!ctx) continue;
            for (auto& [fn, user] : posts) {
                ActiveScope scope(ActiveCounter(*ctx), owner);
                try { fn(user); } catch (...) {}
            }
        }
    }

    const void* JobsApi()
    {
        static const ShardJobsApi api = {
            sizeof(ShardJobsApi), &WorkerCount, &Run, &ParallelForSync, &ParallelForAsync, &IsDone, &Wait, &Release, &PostMain };
        return &api;
    }
}

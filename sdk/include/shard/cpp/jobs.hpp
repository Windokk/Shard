// Shard C++ SDK : the engine's worker threads (shard_jobs.h).
//
//   // bake something off the frame, then hand the result to the simulation
//   auto job = Shard::Jobs::Run([chunk](uint32_t /*worker*/) { chunk->BuildMesh(); });
//   ...
//   job.Wait();                                           // or let it go: ~JobHandle releases it, the work still runs to its end
//
//   Shard::Jobs::ParallelFor(agents.size(), [&](uint32_t i, uint32_t /*worker*/) { agents[i].Steer(dt); });     // returns when all are done
//   Shard::Jobs::PostMain([] { /* back on the main thread, at the start of the next frame */ });
//
// A job may call what the headers document as "thread : any" (blobs, shard.render's resources, shard.debug, the maths...) and the plugin's own code, not the
// actors, the ECS, the physics or the input (they are made for the thread of the simulation). A job that is still running when the plugin is disabled
// is waited for first.
#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <type_traits>
#include <memory>
#include <utility>

#include <shard/shard_jobs.h>
#include <shard/cpp/detail.hpp>

namespace Shard {

    /// A job (or the slices of a ParallelForAsync) in flight. Move only ; letting go of it does not stop the work.
    class JobHandle {
    public:
        JobHandle() = default;
        JobHandle(JobHandle&& other) noexcept : m_Job(other.m_Job) { other.m_Job = 0; }
        JobHandle& operator=(JobHandle&& other) noexcept
        {
            if (this != &other) {
                Release();
                m_Job = other.m_Job;
                other.m_Job = 0;
            }
            return *this;
        }
        JobHandle(const JobHandle&) = delete;
        JobHandle& operator=(const JobHandle&) = delete;
        ~JobHandle() { Release(); }

        explicit JobHandle(ShardJob job) : m_Job(job) {}

        /// True once the work ended (a default handle, or one that ran inline, is done)
        bool IsDone() const { return m_Job == 0 || Api().is_done(detail::PluginHandle(), m_Job) == SHARD_TRUE; }
        /// Returns when the work ended, running other jobs meanwhile. thread : main, script or system
        void Wait()
        {
            if (m_Job) Api().wait(detail::PluginHandle(), m_Job);
            m_Job = 0;
        }

    private:
        void Release()
        {
            if (m_Job) Api().release(detail::PluginHandle(), m_Job);
            m_Job = 0;
        }
        static const ShardJobsApi& Api() { return detail::Require<ShardJobsApi>(SHARD_IFACE_JOBS); }
        ShardJob m_Job = 0;
    };

    class Jobs {
    public:
        /// The threads that work for the engine, the calling one included : the size of a per-worker scratch array
        static uint32_t WorkerCount() { return Api().worker_count(); }

        /// Runs `fn(worker)` on a worker. Keep the handle to Wait for it ; drop it to let the job go on its own.
        template <typename F>
        static JobHandle Run(F&& fn)
        {
            ShardJob job = 0;
            auto* closure = MakeClosure(std::forward<F>(fn));
            if (Api().run(detail::PluginHandle(), &Jobs::RunThunk, closure, &job) != SHARD_OK) {
                delete closure;
                SHARD_THROW(Error(SHARD_E_INTERNAL, "a job could not be started"));
            }
            return JobHandle(job);
        }
        /// The same, without a handle : nothing will wait for it
        template <typename F>
        static void RunDetached(F&& fn)
        {
            auto* closure = MakeClosure(std::forward<F>(fn));
            if (Api().run(detail::PluginHandle(), &Jobs::RunThunk, closure, nullptr) != SHARD_OK) {
                delete closure;
                SHARD_THROW(Error(SHARD_E_INTERNAL, "a job could not be started"));
            }
        }

        /// Calls `fn(index, worker)` for every index of [0, count), on every worker at once, and returns when all are done. The order is not defined :
        /// write to a slot per index, combine in index order. `grain` : how many indices a slice has (0 : the host picks).
        template <typename F>
        static void ParallelFor(uint32_t count, F&& fn, uint32_t grain = 0)
        {
            ParallelForRange(count, [&fn](uint32_t begin, uint32_t end, uint32_t worker) { for (uint32_t i = begin; i < end; ++i) fn(i, worker); }, grain);
        }
        /// `fn(begin, end, worker)` on slices of [0, count) : for a body with a setup per slice
        template <typename F>
        static void ParallelForRange(uint32_t count, F&& fn, uint32_t grain = 0)
        {
            struct Local { F* fn; };
            Local local{ &fn };
            Api().parallel_for(detail::PluginHandle(), count, grain, [](void* user, uint32_t begin, uint32_t end, uint32_t worker) {
                (*static_cast<Local*>(user)->fn)(begin, end, worker);
            }, &local);
        }
        /// The same, without waiting : the closure is kept until the last slice ended
        template <typename F>
        static JobHandle ParallelForAsync(uint32_t count, F&& fn, uint32_t grain = 0)
        {
            if (count == 0) return JobHandle();
            if (grain == 0) grain = std::max<uint32_t>(1, count / (WorkerCount() * 8));
            struct Shared {
                std::function<void(uint32_t, uint32_t, uint32_t)> fn;
                std::atomic<uint32_t> slices;
            };
            auto* shared = new Shared{ std::function<void(uint32_t, uint32_t, uint32_t)>([fn = std::forward<F>(fn)](uint32_t b, uint32_t e, uint32_t w) mutable { for (uint32_t i = b; i < e; ++i) fn(i, w); }),
                                       std::atomic<uint32_t>((count + grain - 1) / grain) };
            ShardJob job = 0;
            if (Api().parallel_for_async(detail::PluginHandle(), count, grain, [](void* user, uint32_t begin, uint32_t end, uint32_t worker) {
                    auto* s = static_cast<Shared*>(user);
                    detail::NoThrow([&] { s->fn(begin, end, worker); });
                    if (s->slices.fetch_sub(1) == 1) delete s;
                }, shared, &job) != SHARD_OK) {
                delete shared;
                SHARD_THROW(Error(SHARD_E_INTERNAL, "a parallel job could not be started"));
            }
            return JobHandle(job);
        }

        /// Runs `fn()` on the main thread at the start of the next frame : how a job hands its result to the simulation. thread : any
        template <typename F>
        static void PostMain(F&& fn)
        {
            auto* closure = new std::function<void()>(std::forward<F>(fn));
            if (Api().post_main(detail::PluginHandle(), [](void* user) {
                    std::unique_ptr<std::function<void()>> f(static_cast<std::function<void()>*>(user));
                    detail::NoThrow([&] { (*f)(); });
                }, closure) != SHARD_OK) {
                delete closure;
            }
        }

    private:
        using Closure = std::function<void(uint32_t)>;
        // A job may take the worker's index or nothing
        template <typename F>
        static Closure* MakeClosure(F&& fn)
        {
            return new Closure([f = std::forward<F>(fn)](uint32_t worker) mutable {
                if constexpr (std::is_invocable_v<std::decay_t<F>&, uint32_t>) f(worker);
                else f();
            });
        }

        static void RunThunk(void* user, uint32_t worker)
        {
            std::unique_ptr<Closure> f(static_cast<Closure*>(user));
            detail::NoThrow([&] { (*f)(worker); });
        }
        static const ShardJobsApi& Api() { return detail::Require<ShardJobsApi>(SHARD_IFACE_JOBS); }
    };
}

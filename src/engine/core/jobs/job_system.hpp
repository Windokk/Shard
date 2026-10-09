#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "engine/core/jobs/work_stealing_deque.hpp"

namespace Shard::Engine::Core {

    class JobSystem;

    /// @brief A set of jobs you can wait on. Submit(group, f) adds one to the count, finishing the job removes it,
    /// JobSystem::Wait(group) returns when it is back to zero. Must outlive its jobs (Wait before it goes out of scope).
    class JobGroup {
    public:
        JobGroup() = default;
        JobGroup(const JobGroup&) = delete;
        JobGroup& operator=(const JobGroup&) = delete;

        bool IsDone() const { return m_Pending.load(std::memory_order_acquire) == 0; }

    private:
        friend class JobSystem;
        std::atomic<uint32_t> m_Pending{0};
    };

    namespace Detail {
        /// Capture storage inside a job. Bigger closures are boxed on the heap (rare, and still correct).
        constexpr size_t kJobInlineSize = 96;
        constexpr size_t kJobInlineAlign = 16;

        /// One schedulable unit. 128 bytes = two cache lines, so two jobs never share a line (false sharing).
        struct alignas(64) Job {
            void (*invoke)(void*) = nullptr;       // runs the closure
            void (*destroy)(void*) = nullptr;      // destroys the closure (and frees the box, if boxed)
            JobGroup* group = nullptr;
            std::atomic<bool> busy{false};         // ring slot in use (owner allocates, executor releases)
            bool heap = false;                     // allocated with new (injection queue): executor deletes it
            alignas(kJobInlineAlign) unsigned char storage[kJobInlineSize];

            template <typename F>
            void Emplace(F&& f) {
                using Fn = std::decay_t<F>;
                if constexpr (sizeof(Fn) <= kJobInlineSize && alignof(Fn) <= kJobInlineAlign) {
                    ::new (static_cast<void*>(storage)) Fn(std::forward<F>(f));
                    invoke = [](void* p) { (*static_cast<Fn*>(p))(); };
                    destroy = [](void* p) { static_cast<Fn*>(p)->~Fn(); };
                } else {
                    ::new (static_cast<void*>(storage)) Fn*(new Fn(std::forward<F>(f)));
                    invoke = [](void* p) { (**static_cast<Fn**>(p))(); };
                    destroy = [](void* p) { delete *static_cast<Fn**>(p); };
                }
            }
        };

        /// Per-thread state: its deque, its ring of preallocated jobs, its steal RNG.
        struct Worker {
            explicit Worker(size_t dequeCapacity) : deque(dequeCapacity), ring(new Job[dequeCapacity]) {}
            WorkStealingDeque<Job*> deque;
            std::unique_ptr<Job[]> ring;           // same size as the deque: a job in the deque always has its own slot
            size_t next = 0;                       // owner-only ring cursor
            uint32_t rng = 0x9E3779B9u;            // xorshift32 state, picks steal victims
            unsigned index = 0;
        };
    }

    struct JobSystemDesc {
        /// Background threads to spawn. ~0u = hardware_concurrency() - 1 (the constructing thread works too, as slot 0).
        unsigned workerCount = ~0u;
        /// Capacity of each thread's deque / job ring (power of two). Beyond it Submit runs the job inline (back-pressure).
        size_t queueCapacity = 4096;
        /// Called first thing on every spawned thread, with its index (1..workerCount). The core layer cannot depend on
        /// the platform layer, so the engine plugs thread naming / priority / affinity in here.
        std::function<void(unsigned workerIndex)> onWorkerStart;
    };

    /// @brief Work-stealing job system.
    ///
    ///  * The thread that constructs the JobSystem is attached as worker 0, so it can Submit cheaply and, inside Wait,
    ///    helps executing jobs. workerCount background threads are spawned next to it.
    ///  * Jobs submitted from a worker go to that worker's deque; idle workers steal. Jobs submitted from any other
    ///    thread go through a mutex-protected injection queue.
    ///  * Wait() never blocks idly: the waiting thread keeps executing jobs, so nested parallelism cannot deadlock and
    ///    a system with workerCount == 0 is still correct (everything runs inside Wait).
    ///  * Jobs must not throw and must not block on something only another job will provide, unless it is a Wait().
    class JobSystem {
    public:
        explicit JobSystem(const JobSystemDesc& desc = {});
        ~JobSystem();

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        /// @brief Schedules f() as part of group. f is copied/moved into the job, so by-value captures are safe, and
        /// by-reference captures are safe as long as you Wait(group) before they die.
        template <typename F>
        void Submit(JobGroup& group, F&& f) {
            Detail::Job* job = AllocJob();
            if (!job) {                            // the ring slot is still in flight: run now instead of growing
                f();
                return;
            }
            job->Emplace(std::forward<F>(f));
            job->group = &group;
            group.m_Pending.fetch_add(1, std::memory_order_relaxed);
            Enqueue(job);
        }

        /// @brief Returns when every job of the group (including those they submitted into it) has finished. The
        /// calling thread executes jobs meanwhile.
        void Wait(JobGroup& group);

        /// @brief Submit + Wait of a single closure, handy for "run this and block".
        template <typename F>
        void Run(F&& f) {
            JobGroup g;
            Submit(g, std::forward<F>(f));
            Wait(g);
        }

        /// @brief Background threads (excludes the attached constructing thread).
        unsigned WorkerCount() const { return static_cast<unsigned>(m_Threads.size()); }
        /// @brief Threads that can run jobs at the same time (workers + the attached thread).
        unsigned ThreadCount() const { return WorkerCount() + 1; }

        /// @brief Grain size that yields ~8 chunks per thread for n items (ParallelFor's default).
        size_t DefaultGrain(size_t n) const;

    private:
        Detail::Job* AllocJob();
        void Enqueue(Detail::Job* job);
        Detail::Job* FindJob(Detail::Worker* self);
        Detail::Job* Steal(Detail::Worker* self);
        void Execute(Detail::Job* job);
        bool TryRunOne(Detail::Worker* self);
        void WorkerMain(unsigned index);
        void Wake();

        JobSystemDesc m_Desc;
        std::vector<std::unique_ptr<Detail::Worker>> m_Workers;   // [0] = attached thread, [1..] = spawned
        std::vector<std::thread> m_Threads;

        std::mutex m_InjectMutex;                                  // jobs from non-worker threads
        std::deque<Detail::Job*> m_Inject;
        std::atomic<size_t> m_InjectSize{0};                       // lock-free "is it empty?" hint for FindJob

        std::mutex m_SleepMutex;                                   // idle workers sleep here
        std::condition_variable m_SleepCv;
        std::atomic<unsigned> m_Sleepers{0};
        std::atomic<int64_t> m_Queued{0};                          // jobs pushed and not yet taken, wake-up predicate
        std::atomic<bool> m_Stop{false};
    };
}

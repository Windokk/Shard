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
        constexpr size_t kJobInlineSize = 96;
        constexpr size_t kJobInlineAlign = 16;

        struct alignas(64) Job {
            void (*invoke)(void*) = nullptr;
            void (*destroy)(void*) = nullptr;
            JobGroup* group = nullptr;
            std::atomic<bool> busy{false};
            bool heap = false;
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

        struct Worker {
            explicit Worker(size_t dequeCapacity) : deque(dequeCapacity), ring(new Job[dequeCapacity]) {}
            WorkStealingDeque<Job*> deque;
            std::unique_ptr<Job[]> ring;
            size_t next = 0;
            uint32_t rng = 0x9E3779B9u;
            unsigned index = 0;
        };
    }

    struct JobSystemDesc {
        unsigned workerCount = ~0u;
        size_t queueCapacity = 4096;
        std::function<void(unsigned workerIndex)> onWorkerStart;
    };

    // Work-stealing. The constructing thread is worker 0 and helps inside Wait(), so nested parallelism cannot deadlock.
    // Jobs must not throw or block on another job except via Wait().
    class JobSystem {
    public:
        explicit JobSystem(const JobSystemDesc& desc = {});
        ~JobSystem();

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        template <typename F>
        void Submit(JobGroup& group, F&& f) {
            Detail::Job* job = AllocJob();
            if (!job) {
                f();
                return;
            }
            job->Emplace(std::forward<F>(f));
            job->group = &group;
            group.m_Pending.fetch_add(1, std::memory_order_relaxed);
            Enqueue(job);
        }

        void Wait(JobGroup& group);

        template <typename F>
        void Run(F&& f) {
            JobGroup g;
            Submit(g, std::forward<F>(f));
            Wait(g);
        }

        unsigned WorkerCount() const { return static_cast<unsigned>(m_Threads.size()); }
        unsigned ThreadCount() const { return WorkerCount() + 1; }

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
        std::vector<std::unique_ptr<Detail::Worker>> m_Workers;
        std::vector<std::thread> m_Threads;

        std::mutex m_InjectMutex;
        std::deque<Detail::Job*> m_Inject;
        std::atomic<size_t> m_InjectSize{0};

        std::mutex m_SleepMutex;
        std::condition_variable m_SleepCv;
        std::atomic<unsigned> m_Sleepers{0};
        std::atomic<int64_t> m_Queued{0};
        std::atomic<bool> m_Stop{false};
    };
}

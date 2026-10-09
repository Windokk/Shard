#include "engine/core/jobs/job_system.hpp"

#include <algorithm>

namespace Shard::Engine::Core {

    using Detail::Job;
    using Detail::Worker;

    namespace {
        // Which JobSystem / Worker the calling thread belongs to (null for threads that never joined one).
        thread_local JobSystem* tl_system = nullptr;
        thread_local Worker* tl_worker = nullptr;
    }

    JobSystem::JobSystem(const JobSystemDesc& desc) : m_Desc(desc) {
        unsigned background = desc.workerCount;
        if (background == ~0u) {
            const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
            background = hw > 1 ? hw - 1 : 0;
        }

        m_Workers.reserve(background + 1);
        for (unsigned i = 0; i <= background; ++i) {
            m_Workers.push_back(std::make_unique<Worker>(desc.queueCapacity));
            m_Workers.back()->index = i;
            m_Workers.back()->rng ^= (i + 1) * 2654435761u;       // distinct non-zero seeds
        }

        tl_system = this;                                          // the constructing thread is worker 0
        tl_worker = m_Workers[0].get();

        m_Threads.reserve(background);
        for (unsigned i = 1; i <= background; ++i)
            m_Threads.emplace_back([this, i] { WorkerMain(i); });
    }

    JobSystem::~JobSystem() {
        {
            std::lock_guard<std::mutex> lock(m_SleepMutex);
            m_Stop.store(true, std::memory_order_seq_cst);
        }
        m_SleepCv.notify_all();
        for (std::thread& t : m_Threads) t.join();
        if (tl_system == this) { tl_system = nullptr; tl_worker = nullptr; }
    }

    size_t JobSystem::DefaultGrain(size_t n) const {
        return std::max<size_t>(1, n / (static_cast<size_t>(ThreadCount()) * 8));
    }

    Job* JobSystem::AllocJob() {
        if (tl_system == this) {
            Worker& w = *tl_worker;
            Job& slot = w.ring[w.next & (w.deque.Capacity() - 1)];
            if (slot.busy.load(std::memory_order_acquire)) return nullptr;   // still queued or running
            ++w.next;
            slot.busy.store(true, std::memory_order_relaxed);
            slot.heap = false;
            return &slot;
        }
        Job* job = new Job();                                      // foreign thread: no ring, heap job
        job->heap = true;
        return job;
    }

    void JobSystem::Enqueue(Job* job) {
        m_Queued.fetch_add(1, std::memory_order_seq_cst);          // BEFORE the push: see the sleep protocol
        if (tl_system == this) {
            if (!tl_worker->deque.Push(job)) {                     // cannot happen (ring == deque size); stay safe
                m_Queued.fetch_sub(1, std::memory_order_relaxed);
                Execute(job);
                return;
            }
        } else {
            std::lock_guard<std::mutex> lock(m_InjectMutex);
            m_Inject.push_back(job);
            m_InjectSize.store(m_Inject.size(), std::memory_order_relaxed);
        }
        Wake();
    }

    void JobSystem::Wake() {
        // Dekker pairing with WorkerMain: we published m_Queued (seq_cst) then read m_Sleepers; a worker publishes
        // m_Sleepers then reads m_Queued. At least one sees the other. Taking the mutex before notifying closes the
        // gap between the worker's predicate check and its actual wait.
        if (m_Sleepers.load(std::memory_order_seq_cst) > 0) {
            std::lock_guard<std::mutex> lock(m_SleepMutex);
            m_SleepCv.notify_one();
        }
    }

    Job* JobSystem::Steal(Worker* self) {
        const size_t n = m_Workers.size();
        if (n <= 1) return nullptr;
        // Random start so that idle thieves do not all hammer the same victim.
        uint32_t x = self ? self->rng : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&n) >> 4) | 1u;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        if (self) self->rng = x;
        const size_t start = x % n;
        for (size_t i = 0; i < n; ++i) {
            Worker* victim = m_Workers[(start + i) % n].get();
            if (victim == self) continue;
            if (Job* job = victim->deque.Steal()) return job;
        }
        return nullptr;
    }

    Job* JobSystem::FindJob(Worker* self) {
        Job* job = self ? self->deque.Pop() : nullptr;             // 1. my own newest job
        if (!job && m_InjectSize.load(std::memory_order_relaxed) > 0) {   // 2. work from foreign threads
            std::lock_guard<std::mutex> lock(m_InjectMutex);
            if (!m_Inject.empty()) {
                job = m_Inject.front();
                m_Inject.pop_front();
                m_InjectSize.store(m_Inject.size(), std::memory_order_relaxed);
            }
        }
        if (!job) job = Steal(self);                               // 3. someone else's oldest job
        if (job) m_Queued.fetch_sub(1, std::memory_order_relaxed);
        return job;
    }

    void JobSystem::Execute(Job* job) {
        job->invoke(job->storage);
        job->destroy(job->storage);
        JobGroup* group = job->group;
        if (job->heap) delete job;
        else job->busy.store(false, std::memory_order_release);   // after this the owner may reuse the slot
        // Last: once the count hits zero the waiter may return and destroy the group, so nothing may touch it after.
        group->m_Pending.fetch_sub(1, std::memory_order_acq_rel);
    }

    bool JobSystem::TryRunOne(Worker* self) {
        Job* job = FindJob(self);
        if (!job) return false;
        Execute(job);
        return true;
    }

    void JobSystem::Wait(JobGroup& group) {
        Worker* self = (tl_system == this) ? tl_worker : nullptr;
        while (group.m_Pending.load(std::memory_order_acquire) != 0) {
            if (!TryRunOne(self)) std::this_thread::yield();      // others are running our jobs: let them
        }
    }

    void JobSystem::WorkerMain(unsigned index) {
        tl_system = this;
        tl_worker = m_Workers[index].get();
        if (m_Desc.onWorkerStart) m_Desc.onWorkerStart(index);

        Worker* self = tl_worker;
        unsigned idleSpins = 0;
        while (!m_Stop.load(std::memory_order_acquire)) {
            if (TryRunOne(self)) { idleSpins = 0; continue; }
            if (++idleSpins < 64) { std::this_thread::yield(); continue; }   // short spin: work often arrives within µs

            std::unique_lock<std::mutex> lock(m_SleepMutex);
            m_Sleepers.fetch_add(1, std::memory_order_seq_cst);
            m_SleepCv.wait(lock, [this] {
                return m_Stop.load(std::memory_order_seq_cst) || m_Queued.load(std::memory_order_seq_cst) > 0;
            });
            m_Sleepers.fetch_sub(1, std::memory_order_seq_cst);
            idleSpins = 0;
        }
        tl_system = nullptr;
        tl_worker = nullptr;
    }
}

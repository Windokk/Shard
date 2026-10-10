#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

#include "engine/core/jobs/job_system.hpp"

namespace Shard::Engine::Physics {

    /// @brief Jolt's JobSystem on top of the engine's job system, so that physics shares the machine's cores with
    /// everything else instead of running a thread pool of its own next to it.
    ///
    /// Jolt's barrier handling (JobSystemWithBarrier) does the waiting : a barrier's Wait() executes the jobs of
    /// that barrier on the calling thread, and blocks only for the ones other threads are running. What is left for
    /// this class is where Jolt jobs live (a fixed free list, as in Jolt's own pool) and how they get run : each
    /// queued Jolt job becomes one job of the engine's job system.
    class JoltJobSystem final : public JPH::JobSystemWithBarrier {
    public:
        JoltJobSystem(Core::JobSystem& jobs, JPH::uint maxJobs, JPH::uint maxBarriers)
            : JPH::JobSystemWithBarrier(maxBarriers), m_Jobs(jobs) {
            m_Pool.Init(maxJobs, maxJobs);
        }

        /// Jobs already queued run to the end before the free list they live in goes away.
        ~JoltJobSystem() override { m_Jobs.Wait(m_Group); }

        int GetMaxConcurrency() const override { return static_cast<int>(m_Jobs.ThreadCount()); }

        JPH::JobHandle CreateJob(const char* name, JPH::ColorArg color, const JPH::JobSystem::JobFunction& function,
                                 JPH::uint32 numDependencies = 0) override {
            // Same policy as Jolt's thread pool : the free list is sized for the worst case, if it is ever empty
            // wait for jobs to finish and retry
            JPH::uint32 index;
            for (;;) {
                index = m_Pool.ConstructObject(name, color, this, function, numDependencies);
                if (index != AvailableJobs::cInvalidObjectIndex) break;
                JPH_ASSERT(false, "No jobs available!");
                std::this_thread::yield();
            }
            Job* job = &m_Pool.Get(index);

            JPH::JobHandle handle(job);     // holds a reference : the job may finish before this function returns
            if (numDependencies == 0) QueueJob(job);
            return handle;
        }

    protected:
        void QueueJob(Job* job) override {
            job->AddRef();                  // the queued closure owns a reference until it has run
            m_Jobs.Submit(m_Group, [job] {
                job->Execute();             // a no-op if a barrier's Wait() already ran it on its own thread
                job->Release();
            });
        }

        void QueueJobs(Job** jobs, JPH::uint count) override {
            for (JPH::uint i = 0; i < count; ++i) QueueJob(jobs[i]);
        }

        void FreeJob(Job* job) override { m_Pool.DestructObject(job); }

    private:
        using AvailableJobs = JPH::FixedSizeFreeList<Job>;

        Core::JobSystem& m_Jobs;
        Core::JobGroup m_Group;             // every queued Jolt job, only waited on at destruction
        AvailableJobs m_Pool;
    };
}

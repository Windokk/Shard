#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

#include "engine/core/jobs/job_system.hpp"

namespace Shard::Engine::Physics {

    class JoltJobSystem final : public JPH::JobSystemWithBarrier {
    public:
        JoltJobSystem(Core::JobSystem& jobs, JPH::uint maxJobs, JPH::uint maxBarriers)
            : JPH::JobSystemWithBarrier(maxBarriers), m_Jobs(jobs) {
            m_Pool.Init(maxJobs, maxJobs);
        }

        ~JoltJobSystem() override { m_Jobs.Wait(m_Group); }

        int GetMaxConcurrency() const override { return static_cast<int>(m_Jobs.ThreadCount()); }

        JPH::JobHandle CreateJob(const char* name, JPH::ColorArg color, const JPH::JobSystem::JobFunction& function,
                                 JPH::uint32 numDependencies = 0) override {
            JPH::uint32 index;
            for (;;) {
                index = m_Pool.ConstructObject(name, color, this, function, numDependencies);
                if (index != AvailableJobs::cInvalidObjectIndex) break;
                JPH_ASSERT(false, "No jobs available!");
                std::this_thread::yield();
            }
            Job* job = &m_Pool.Get(index);

            JPH::JobHandle handle(job);
            if (numDependencies == 0) QueueJob(job);
            return handle;
        }

    protected:
        void QueueJob(Job* job) override {
            job->AddRef();
            m_Jobs.Submit(m_Group, [job] {
                job->Execute();
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
        Core::JobGroup m_Group;
        AvailableJobs m_Pool;
    };
}

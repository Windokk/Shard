#pragma once

#include <cstddef>
#include <type_traits>

#include "engine/core/jobs/job_system.hpp"

namespace Shard::Engine::Core {

    namespace Detail {
        // Keep the left half, hand the right half to the pool, repeat down to grain. Splitting is itself parallel. Thieves take the oldest (biggest) half.
        template <typename F>
        void SplitRange(JobSystem& js, JobGroup& group, size_t begin, size_t end, size_t grain, F& fn) {
            while (end - begin > grain) {
                const size_t mid = begin + (end - begin) / 2;
                js.Submit(group, [&js, &group, mid, end, grain, &fn] { SplitRange(js, group, mid, end, grain, fn); });
                end = mid;
            }
            fn(begin, end);
        }
    }

    template <typename F>
    void ParallelForRange(JobSystem& js, size_t begin, size_t end, size_t grain, F&& fn) {
        if (begin >= end) return;
        if (grain == 0) grain = js.DefaultGrain(end - begin);
        if (end - begin <= grain) { fn(begin, end); return; }
        JobGroup group;
        Detail::SplitRange(js, group, begin, end, grain, fn);
        js.Wait(group);
    }

    template <typename F>
    void ParallelFor(JobSystem& js, size_t begin, size_t end, size_t grain, F&& fn) {
        ParallelForRange(js, begin, end, grain, [&fn](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) fn(i);
        });
    }

    template <typename F>
    void ParallelFor(JobSystem& js, size_t begin, size_t end, F&& fn) { ParallelFor(js, begin, end, 0, std::forward<F>(fn)); }
}

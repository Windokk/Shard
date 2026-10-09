#pragma once

#include <cstddef>
#include <type_traits>

#include "engine/core/jobs/job_system.hpp"

namespace Shard::Engine::Core {

    namespace Detail {
        /// Divide & conquer: keep the left half for ourselves, hand the right half to the pool, repeat until a chunk
        /// is <= grain. The submitted job runs this same function on its half, so the splitting itself is parallel
        /// (log2 depth instead of a serial loop that submits n/grain jobs). Thieves take the OLDEST deque entry, which
        /// is the biggest remaining half: a single steal transfers a large block of work.
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

    /// @brief Calls fn(chunkBegin, chunkEnd) over disjoint chunks covering [begin, end), in parallel, and returns when
    /// all are done. Chunks are at most `grain` long (0 = automatic, ~8 chunks per thread). Use this form when the body
    /// has per-chunk setup (a local accumulator, a scratch buffer); it also lets the compiler vectorise the inner loop.
    template <typename F>
    void ParallelForRange(JobSystem& js, size_t begin, size_t end, size_t grain, F&& fn) {
        if (begin >= end) return;
        if (grain == 0) grain = js.DefaultGrain(end - begin);
        if (end - begin <= grain) { fn(begin, end); return; }          // not worth a job
        JobGroup group;
        Detail::SplitRange(js, group, begin, end, grain, fn);
        js.Wait(group);
    }

    /// @brief Calls fn(i) for every i in [begin, end), in parallel. Iterations must be independent (no ordering, no
    /// two writing the same memory without synchronisation). Pick `grain` so that one chunk costs well over ~1 us.
    template <typename F>
    void ParallelFor(JobSystem& js, size_t begin, size_t end, size_t grain, F&& fn) {
        ParallelForRange(js, begin, end, grain, [&fn](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) fn(i);
        });
    }

    template <typename F>
    void ParallelFor(JobSystem& js, size_t begin, size_t end, F&& fn) { ParallelFor(js, begin, end, 0, std::forward<F>(fn)); }
}

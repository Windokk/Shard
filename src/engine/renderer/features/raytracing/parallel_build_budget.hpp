#pragma once

#include <algorithm>
#include <atomic>
#include <thread>

// Process-wide cap on how many EXTRA worker threads scene-building code is allowed to spawn at once -
// shared across every concurrent caller, not scoped to a single one. Both the triangle flatten pass and
// the BVH build (raytrace_scene.cpp / bvh.cpp) draw from this same pool.
//
// Why this needs to be shared rather than each caller computing its own hardware_concurrency()-sized
// budget : ProbeManager::RebuildScene() can be called again before a previous build finishes (e.g. a
// ProbeVolume activates twice while a level loads, or a level is closed and reopened right away) - the
// previous build isn't cancelled, just superseded (see that function's comment), so it keeps running in
// the background. Loading a further level shortly after can start a third build on top of that. With
// each Build()/BuildFromSnapshot() call independently assuming it owns the whole machine, two or three
// overlapping builds each spawn their own full batch of worker threads - past the OS's thread limit,
// creating one more throws std::system_error. Sharing one budget across all of them keeps the total
// worker count bounded regardless of how many builds happen to overlap.
//
// TryAcquireWorkerSlot()/ReleaseWorkerSlot() alone only prevent the pile-up from growing unbounded, not
// std::async failing for some other reason (out of memory, a platform-specific limit unrelated to plain
// thread count, ...) - every actual std::async(std::launch::async, ...) call built on top of this budget
// must still be wrapped in a try/catch that falls back to running the work on the calling thread instead
// of letting the exception propagate. An uncaught std::system_error from a background scene-build thread
// ends up rethrown on the main thread the moment ProbeManager::Update() calls future::get() on it -
// an unhandled exception there crashes the whole editor.
namespace Shard::Engine::Rendering::Raytracing {

    inline std::atomic<int>& GlobalWorkerBudget()
    {
        static std::atomic<int> budget{ std::max(1, (int)std::thread::hardware_concurrency() - 1) };
        return budget;
    }

    // Claims one slot from the shared budget. Returns false (no slot claimed) if none are free right
    // now - the caller should just run the work on the current thread instead of spawning. Every
    // successful claim must be matched by exactly one ReleaseWorkerSlot() call, whether or not the
    // thread creation attempted with it actually succeeds.
    inline bool TryAcquireWorkerSlot()
    {
        std::atomic<int>& budget = GlobalWorkerBudget();
        int slots = budget.load(std::memory_order_relaxed);
        while (slots > 0)
        {
            if (budget.compare_exchange_weak(slots, slots - 1, std::memory_order_relaxed))
                return true;
        }
        return false;
    }

    inline void ReleaseWorkerSlot()
    {
        GlobalWorkerBudget().fetch_add(1, std::memory_order_relaxed);
    }

}

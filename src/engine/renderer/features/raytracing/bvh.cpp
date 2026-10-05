#include "bvh.hpp"
#include "parallel_build_budget.hpp"

#include <algorithm>
#include <atomic>
#include <future>
#include <limits>
#include <system_error>
#include <thread>

namespace Shard::Engine::Rendering::Raytracing {

    namespace {

        constexpr int kSAHBins = 12;
        constexpr uint32_t kMaxLeafTriangles = 4;

        // Below this triangle count, offloading a subtree to another thread costs more (thread
        // wakeup + the eventual join) than it saves - triCount roughly halves with every split, so only
        // the top handful of levels of a large build are ever above this and worth parallelizing at all.
        constexpr uint32_t kMinTrianglesToParallelize = 8192;

        float SurfaceArea(const glm::vec3& boundsMin, const glm::vec3& boundsMax)
        {
            glm::vec3 e = boundsMax - boundsMin;
            if (e.x < 0.0f || e.y < 0.0f || e.z < 0.0f)
                return 0.0f;
            return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
        }

        void UpdateBounds(BVHNode& node, const std::vector<BVHPrimitive>& prims, const std::vector<uint32_t>& order)
        {
            glm::vec3 boundsMin(std::numeric_limits<float>::max());
            glm::vec3 boundsMax(std::numeric_limits<float>::lowest());

            for (uint32_t i = 0; i < node.triCount; i++)
            {
                const BVHPrimitive& p = prims[order[node.leftFirst + i]];
                boundsMin = glm::min(boundsMin, p.boundsMin);
                boundsMax = glm::max(boundsMax, p.boundsMax);
            }

            node.boundsMin = boundsMin;
            node.boundsMax = boundsMax;
        }

        // Binned SAH : bins primitive centroids along each axis into kSAHBins buckets, then evaluates
        // the surface-area-heuristic cost for every candidate plane between buckets and keeps the best
        // one found across all 3 axes. Returns false if no axis has any centroid spread at all (every
        // primitive in the node shares the same centroid - can't usefully split further).
        bool FindBestSplit(const BVHNode& node, const std::vector<BVHPrimitive>& prims, const std::vector<uint32_t>& order,
            int& outAxis, float& outSplitPos, float& outCost)
        {
            outCost = std::numeric_limits<float>::max();
            bool found = false;

            for (int axis = 0; axis < 3; axis++)
            {
                float centroidMin = std::numeric_limits<float>::max();
                float centroidMax = std::numeric_limits<float>::lowest();

                for (uint32_t i = 0; i < node.triCount; i++)
                {
                    float c = prims[order[node.leftFirst + i]].centroid[axis];
                    centroidMin = std::min(centroidMin, c);
                    centroidMax = std::max(centroidMax, c);
                }

                if (centroidMax - centroidMin < 1e-8f)
                    continue;

                struct Bin
                {
                    glm::vec3 boundsMin{ std::numeric_limits<float>::max() };
                    glm::vec3 boundsMax{ std::numeric_limits<float>::lowest() };
                    uint32_t count = 0;
                };

                Bin bins[kSAHBins];
                float scale = kSAHBins / (centroidMax - centroidMin);

                for (uint32_t i = 0; i < node.triCount; i++)
                {
                    const BVHPrimitive& p = prims[order[node.leftFirst + i]];
                    int binIdx = std::min(kSAHBins - 1, (int)((p.centroid[axis] - centroidMin) * scale));
                    bins[binIdx].count++;
                    bins[binIdx].boundsMin = glm::min(bins[binIdx].boundsMin, p.boundsMin);
                    bins[binIdx].boundsMax = glm::max(bins[binIdx].boundsMax, p.boundsMax);
                }

                // Sweep left->right and right->left to get, for each of the (kSAHBins - 1) candidate
                // planes, the cumulative count/surface-area of everything to its left and to its right.
                float leftArea[kSAHBins - 1];
                float rightArea[kSAHBins - 1];
                uint32_t leftCount[kSAHBins - 1];
                uint32_t rightCount[kSAHBins - 1];

                glm::vec3 lMin(std::numeric_limits<float>::max());
                glm::vec3 lMax(std::numeric_limits<float>::lowest());
                uint32_t lCount = 0;

                for (int i = 0; i < kSAHBins - 1; i++)
                {
                    lCount += bins[i].count;
                    lMin = glm::min(lMin, bins[i].boundsMin);
                    lMax = glm::max(lMax, bins[i].boundsMax);
                    leftCount[i] = lCount;
                    leftArea[i] = lCount > 0 ? SurfaceArea(lMin, lMax) : 0.0f;
                }

                glm::vec3 rMin(std::numeric_limits<float>::max());
                glm::vec3 rMax(std::numeric_limits<float>::lowest());
                uint32_t rCount = 0;

                for (int i = kSAHBins - 1; i >= 1; i--)
                {
                    rCount += bins[i].count;
                    rMin = glm::min(rMin, bins[i].boundsMin);
                    rMax = glm::max(rMax, bins[i].boundsMax);
                    rightCount[i - 1] = rCount;
                    rightArea[i - 1] = rCount > 0 ? SurfaceArea(rMin, rMax) : 0.0f;
                }

                float binWidth = (centroidMax - centroidMin) / kSAHBins;

                for (int i = 0; i < kSAHBins - 1; i++)
                {
                    float cost = leftCount[i] * leftArea[i] + rightCount[i] * rightArea[i];
                    if (cost < outCost)
                    {
                        outCost = cost;
                        outAxis = axis;
                        outSplitPos = centroidMin + binWidth * (i + 1);
                        found = true;
                    }
                }
            }

            return found;
        }

        struct BuildProgress
        {
            const BVHBuilder::ProgressFn* fn = nullptr;
            size_t total = 0;
            // Every primitive is accounted for exactly once (a split node's triangles are counted later,
            // by its descendants' leaves), so `done` walks monotonically from 0 to `total` overall - but
            // once Subdivide() below can offload a subtree to another thread, leaf() itself is called
            // concurrently from multiple threads, hence atomic rather than a plain counter.
            std::atomic<size_t> done{ 0 };
            // Highest fraction actually reported so far. Two threads racing to finish a leaf can call
            // leaf() with `done` values that don't reflect wall-clock completion order any more (the
            // primitive each leaf covers no longer maps to "how far along the build is" once work is
            // split across threads) - onProgress's contract (see bvh.hpp) is a monotonically increasing
            // fraction, so this clamps every call to a new high-water mark instead of ever reporting
            // backward.
            std::atomic<float> reported{ 0.0f };

            void leaf(uint32_t triCount)
            {
                size_t newDone = done.fetch_add(triCount, std::memory_order_relaxed) + triCount;
                if (!fn || !*fn)
                    return;

                float frac = total ? (float)newDone / (float)total : 1.0f;

                float prev = reported.load(std::memory_order_relaxed);
                while (frac > prev)
                {
                    if (reported.compare_exchange_weak(prev, frac, std::memory_order_relaxed))
                    {
                        (*fn)(frac);
                        break;
                    }
                }
            }
        };

        void Subdivide(std::vector<BVHNode>& nodes, std::atomic<uint32_t>& nodesUsed, uint32_t nodeIdx,
            const std::vector<BVHPrimitive>& prims, std::vector<uint32_t>& order, BuildProgress& progress)
        {
            BVHNode& node = nodes[nodeIdx];

            if (node.triCount <= kMaxLeafTriangles)
            {
                progress.leaf(node.triCount);
                return;
            }

            int axis = 0;
            float splitPos = 0.0f, splitCost = 0.0f;
            if (!FindBestSplit(node, prims, order, axis, splitPos, splitCost))
            {
                progress.leaf(node.triCount);
                return;
            }

            // Only split if it's actually cheaper than leaving this node as one big leaf.
            float parentCost = node.triCount * SurfaceArea(node.boundsMin, node.boundsMax);
            if (splitCost >= parentCost)
            {
                progress.leaf(node.triCount);
                return;
            }

            // Partition [leftFirst, leftFirst + triCount) in-place by centroid vs. the chosen plane.
            auto rangeBegin = order.begin() + node.leftFirst;
            auto rangeEnd = rangeBegin + node.triCount;
            auto mid = std::partition(rangeBegin, rangeEnd, [&](uint32_t primIdx) {
                return prims[primIdx].centroid[axis] < splitPos;
            });

            uint32_t leftCount = (uint32_t)std::distance(rangeBegin, mid);
            if (leftCount == 0 || leftCount == node.triCount)
            {
                progress.leaf(node.triCount); // degenerate split (all centroids landed on one side) - keep as leaf
                return;
            }

            uint32_t leftFirst = node.leftFirst;
            uint32_t rightFirst = leftFirst + leftCount;
            uint32_t rightCount = node.triCount - leftCount;

            // nodesUsed is shared with every other in-flight branch of this build (see the offload
            // below), so this pair of indices has to be claimed as a single atomic step. The GPU
            // traversal (raytracing_trace.glsl) hard-codes "right child = leftFirst + 1" and only ever
            // stores leftFirst, so the two indices MUST come out consecutive - two separate fetch_add(1)
            // calls would let another thread's own fetch_add land in between them under concurrency,
            // silently breaking that invariant (the node's stored leftFirst would still be right, but
            // leftFirst + 1 would be some unrelated node from a different branch of the build).
            uint32_t leftIdx = nodesUsed.fetch_add(2, std::memory_order_relaxed);
            uint32_t rightIdx = leftIdx + 1;

            nodes[leftIdx].leftFirst = leftFirst;
            nodes[leftIdx].triCount = leftCount;
            nodes[rightIdx].leftFirst = rightFirst;
            nodes[rightIdx].triCount = rightCount;

            // node.leftFirst now refers to a node index (not a triangle index) - reinterpreted per the
            // triCount == 0 convention.
            node.leftFirst = leftIdx;
            node.triCount = 0;

            UpdateBounds(nodes[leftIdx], prims, order);
            UpdateBounds(nodes[rightIdx], prims, order);

            // The two subtrees only ever touch disjoint node indices (freshly claimed above) and disjoint
            // ranges of `order` (left/right of the partition just performed), so running them on separate
            // threads needs no locking. Only the right side is ever offloaded - the calling thread always
            // continues on the left - so a small build (or a call already deep in the tree, below
            // kMinTrianglesToParallelize) never touches std::async at all.
            //
            // The slot comes from the process-wide budget in parallel_build_budget.hpp, not a per-Build()
            // one : this Subdivide() call can be running as part of a build that superseded (but didn't
            // cancel) another one still in flight - see ProbeManager::RebuildScene()'s comment - and each
            // Build() call computing its own hardware_concurrency()-sized budget independently is exactly
            // how two or three overlapping builds used to pile up more OS threads than the machine (or
            // std::async's underlying implementation) could actually hand out.
            if (rightCount >= kMinTrianglesToParallelize && TryAcquireWorkerSlot())
            {
                try
                {
                    std::future<void> rightTask = std::async(std::launch::async, [&]() {
                        Subdivide(nodes, nodesUsed, rightIdx, prims, order, progress);
                        ReleaseWorkerSlot();
                    });

                    Subdivide(nodes, nodesUsed, leftIdx, prims, order, progress);
                    rightTask.wait();
                    return;
                }
                catch (const std::system_error&)
                {
                    // std::async's constructor throws synchronously if it can't start a new thread (e.g.
                    // the OS/CRT thread limit was hit) - the lambda above never ran in that case, so the
                    // slot was never going to be released by it; give it back here and fall through to
                    // the plain serial path below instead of losing the right subtree or propagating.
                    // Letting this escape uncaught is what used to turn "couldn't spawn one more thread"
                    // into an unhandled exception on the main thread the next time
                    // ProbeManager::Update() called future::get() on some pending build - i.e. a crash
                    // with no obvious connection to the actual cause.
                    ReleaseWorkerSlot();
                }
            }

            Subdivide(nodes, nodesUsed, leftIdx, prims, order, progress);
            Subdivide(nodes, nodesUsed, rightIdx, prims, order, progress);
        }

    }

    std::vector<BVHNode> BVHBuilder::Build(const std::vector<BVHPrimitive>& primitives, std::vector<uint32_t>& outOrder,
        const ProgressFn& onProgress)
    {
        size_t n = primitives.size();

        outOrder.resize(n);
        for (size_t i = 0; i < n; i++)
            outOrder[i] = (uint32_t)i;

        if (n == 0)
            return { BVHNode{ glm::vec3(0.0f), 0, glm::vec3(0.0f), 0 } };

        // Upper bound on total node count for a binary tree over n leaves : each split adds exactly 2
        // nodes and there are at most n - 1 splits, so 2n - 1 nodes covers every case. Allocated
        // up front (fixed size, no push_back) so the BVHNode& references taken during Subdivide are
        // never invalidated by a reallocation.
        std::vector<BVHNode> nodes(2 * n);
        std::atomic<uint32_t> nodesUsed{ 1 };

        nodes[0].leftFirst = 0;
        nodes[0].triCount = (uint32_t)n;
        UpdateBounds(nodes[0], primitives, outOrder);

        BuildProgress progress;
        progress.fn = &onProgress;
        progress.total = n;

        Subdivide(nodes, nodesUsed, 0, primitives, outOrder, progress);

        if (onProgress)
            onProgress(1.0f);

        nodes.resize(nodesUsed.load(std::memory_order_relaxed));
        return nodes;
    }

}

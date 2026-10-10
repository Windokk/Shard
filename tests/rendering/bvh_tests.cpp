#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "engine/core/jobs/job_system.hpp"
#include "engine/renderer/features/raytracing/bvh.hpp"
#include "support/test_engine_context.hpp"

using namespace Shard::Engine::Rendering::Raytracing;
using Shard::Engine::Core::JobSystem;
using Shard::Engine::Core::JobSystemDesc;

namespace {
    std::vector<BVHPrimitive> RandomTriangles(size_t n) {
        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> pos(-100.0f, 100.0f), size(0.01f, 1.0f);
        std::vector<BVHPrimitive> prims(n);
        for (BVHPrimitive& p : prims) {
            glm::vec3 c(pos(rng), pos(rng), pos(rng));
            glm::vec3 e(size(rng), size(rng), size(rng));
            p.boundsMin = c - e;
            p.boundsMax = c + e;
            p.centroid = c;
        }
        return prims;
    }

    // Walks the tree and checks what the GPU traversal relies on. Returns the number of triangles reached.
    uint32_t CheckNode(const std::vector<BVHNode>& nodes, uint32_t idx, const std::vector<BVHPrimitive>& prims,
                       const std::vector<uint32_t>& order, std::vector<int>& seenSlots) {
        const BVHNode& n = nodes[idx];
        if (n.triCount > 0) {                                           // leaf : its triangles are inside its bounds
            for (uint32_t i = 0; i < n.triCount; ++i) {
                ++seenSlots[n.leftFirst + i];
                const BVHPrimitive& p = prims[order[n.leftFirst + i]];
                EXPECT_TRUE(glm::all(glm::greaterThanEqual(p.boundsMin, n.boundsMin)));
                EXPECT_TRUE(glm::all(glm::lessThanEqual(p.boundsMax, n.boundsMax)));
            }
            return n.triCount;
        }
        const uint32_t left = n.leftFirst, right = n.leftFirst + 1;      // the GPU hard-codes "right = left + 1"
        for (uint32_t child : {left, right}) {
            EXPECT_TRUE(glm::all(glm::greaterThanEqual(nodes[child].boundsMin, n.boundsMin)));
            EXPECT_TRUE(glm::all(glm::lessThanEqual(nodes[child].boundsMax, n.boundsMax)));
        }
        return CheckNode(nodes, left, prims, order, seenSlots) + CheckNode(nodes, right, prims, order, seenSlots);
    }

    void BuildAndCheck(size_t count) {
        std::vector<BVHPrimitive> prims = RandomTriangles(count);
        std::vector<uint32_t> order;
        std::atomic<int> progressCalls{0};
        std::vector<BVHNode> nodes = BVHBuilder::Build(prims, order, [&](float) { progressCalls.fetch_add(1); });

        ASSERT_EQ(order.size(), count);
        std::vector<int> originalSeen(count, 0);
        for (uint32_t o : order) { ASSERT_LT(o, count); ++originalSeen[o]; }
        for (size_t i = 0; i < count; ++i) ASSERT_EQ(originalSeen[i], 1) << "primitive " << i;   // a permutation

        std::vector<int> slotSeen(count, 0);
        EXPECT_EQ(CheckNode(nodes, 0, prims, order, slotSeen), count);
        for (size_t i = 0; i < count; ++i) ASSERT_EQ(slotSeen[i], 1) << "slot " << i;           // leaves partition [0, n)
    }
}

TEST(BVHBuilder, SerialBuildWithoutAJobSystem) {
    Shard::Tests::TestEngineContext context;              // jobSystem == nullptr : everything runs on this thread
    Shard::Engine::Core::SetEngine(&context);
    BuildAndCheck(30000);
    Shard::Engine::Core::SetEngine(nullptr);
}

TEST(BVHBuilder, ParallelBuildOnTheJobSystemIsAValidTree) {
    JobSystemDesc desc;
    desc.workerCount = 3;
    JobSystem jobs(desc);
    Shard::Tests::TestEngineContext context;
    context.jobSystem = &jobs;
    Shard::Engine::Core::SetEngine(&context);
    for (int rep = 0; rep < 5; ++rep) BuildAndCheck(60000);   // the top levels are above the parallelisation threshold
    Shard::Engine::Core::SetEngine(nullptr);
}

TEST(BVHBuilder, ConcurrentBuildsShareOnePool) {
    // What happens when ProbeManager::RebuildScene() is called again before the previous build finished
    JobSystemDesc desc;
    desc.workerCount = 2;
    JobSystem jobs(desc);
    Shard::Tests::TestEngineContext context;
    context.jobSystem = &jobs;
    Shard::Engine::Core::SetEngine(&context);
    std::vector<std::thread> builders;
    for (int i = 0; i < 3; ++i) builders.emplace_back([] { BuildAndCheck(40000); });
    for (auto& t : builders) t.join();
    Shard::Engine::Core::SetEngine(nullptr);
}

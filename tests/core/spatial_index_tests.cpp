#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <random>

#include "engine/core/spatial/spatial_index.hpp"

using namespace Shard::Engine::Core;

namespace {
    using Id = ISpatialIndex::Id;

    std::unique_ptr<ISpatialIndex> Make(SpatialIndexKind kind) {
        SpatialIndexConfig c;
        c.kind = kind;
        c.gridCellSize = 16.0;
        c.octreeHalfSize = 256.0;
        c.octreeMaxDepth = 6;
        return CreateSpatialIndex(c);
    }

    AABB RandomBox(std::mt19937_64& rng, double range, double maxSize) {
        std::uniform_real_distribution<double> pos(-range, range);
        std::uniform_real_distribution<double> size(0.0, maxSize);
        const DVec3 c(pos(rng), pos(rng), pos(rng));
        return AABB::FromCenterExtents(c, DVec3(size(rng), size(rng), size(rng)));
    }

    std::vector<Id> Brute(const std::map<Id, AABB>& items, const AABB& q) {
        std::vector<Id> r;
        for (const auto& [id, b] : items) if (b.Overlaps(q)) r.push_back(id);
        return r;
    }

    class SpatialIndexTest : public ::testing::TestWithParam<SpatialIndexKind> {};
}

INSTANTIATE_TEST_SUITE_P(AllKinds, SpatialIndexTest,
    ::testing::Values(SpatialIndexKind::BVH, SpatialIndexKind::Grid, SpatialIndexKind::Octree));

TEST_P(SpatialIndexTest, InsertQueryRemove) {
    auto idx = Make(GetParam());
    EXPECT_TRUE(idx->Insert(1, AABB({0, 0, 0}, {1, 1, 1})));
    EXPECT_TRUE(idx->Insert(2, AABB({10, 10, 10}, {11, 11, 11})));
    EXPECT_EQ(idx->Size(), 2u);

    std::vector<Id> out;
    idx->QueryAABB(AABB({-1, -1, -1}, {2, 2, 2}), out);
    EXPECT_EQ(out, (std::vector<Id>{1}));

    EXPECT_TRUE(idx->Remove(1));
    EXPECT_FALSE(idx->Remove(1));
    idx->QueryAABB(AABB({-100, -100, -100}, {100, 100, 100}), out);
    EXPECT_EQ(out, (std::vector<Id>{2}));
    idx->Clear();
    EXPECT_EQ(idx->Size(), 0u);
}

TEST_P(SpatialIndexTest, RejectsBadBoxes) {
    auto idx = Make(GetParam());
    EXPECT_FALSE(idx->Insert(1, AABB()));
    EXPECT_FALSE(idx->Insert(1, AABB::Infinite()));
    EXPECT_FALSE(idx->Insert(1, AABB({std::nan(""), 0, 0}, {1, 1, 1})));
    EXPECT_EQ(idx->Size(), 0u);
    EXPECT_FALSE(idx->Update(1, AABB({0, 0, 0}, {1, 1, 1})));
}

TEST_P(SpatialIndexTest, ReinsertingAnIdMovesIt) {
    auto idx = Make(GetParam());
    idx->Insert(7, AABB({0, 0, 0}, {1, 1, 1}));
    idx->Insert(7, AABB({50, 50, 50}, {51, 51, 51}));
    EXPECT_EQ(idx->Size(), 1u);
    std::vector<Id> out;
    idx->QueryAABB(AABB({-1, -1, -1}, {2, 2, 2}), out);
    EXPECT_TRUE(out.empty());
    idx->QueryAABB(AABB({49, 49, 49}, {52, 52, 52}), out);
    EXPECT_EQ(out, (std::vector<Id>{7}));
}

TEST_P(SpatialIndexTest, MatchesBruteForceUnderChurn) {
    auto idx = Make(GetParam());
    std::map<Id, AABB> ref;
    std::mt19937_64 rng(1234);
    std::uniform_int_distribution<int> op(0, 9);

    for (Id id = 0; id < 400; ++id) {
        // Some items are far bigger than the rest, some are outside the octree's cube
        const double size = (id % 50 == 0) ? 120.0 : 3.0;
        const AABB b = RandomBox(rng, id % 37 == 0 ? 2000.0 : 200.0, size);
        ASSERT_TRUE(idx->Insert(id, b));
        ref[id] = b;
    }

    std::vector<Id> got;
    for (int step = 0; step < 1500; ++step) {
        const Id id = rng() % 450;
        switch (op(rng)) {
            case 0: case 1: case 2: {                   // move
                const AABB b = RandomBox(rng, 200.0, 4.0);
                if (ref.count(id)) { EXPECT_TRUE(idx->Update(id, b)); ref[id] = b; }
                else EXPECT_FALSE(idx->Update(id, b));
                break;
            }
            case 3: {                                   // small move (stays in the BVH fat box)
                auto it = ref.find(id);
                if (it == ref.end()) break;
                AABB b = it->second;
                b.min += DVec3(0.01); b.max += DVec3(0.01);
                EXPECT_TRUE(idx->Update(id, b));
                it->second = b;
                break;
            }
            case 4: {
                EXPECT_EQ(idx->Remove(id), ref.erase(id) == 1);
                break;
            }
            case 5: {
                const AABB b = RandomBox(rng, 200.0, 5.0);
                idx->Insert(id, b);
                ref[id] = b;
                break;
            }
            default: break;
        }

        const AABB q = RandomBox(rng, 220.0, 40.0);
        idx->QueryAABB(q, got);
        ASSERT_EQ(got, Brute(ref, q)) << "step " << step;
    }
    EXPECT_EQ(idx->Size(), ref.size());
    for (const auto& [id, b] : ref) {
        AABB stored;
        ASSERT_TRUE(idx->GetBox(id, stored));
        EXPECT_EQ(stored.min, b.min);
        EXPECT_EQ(stored.max, b.max);
    }
}

TEST_P(SpatialIndexTest, SphereQuery) {
    auto idx = Make(GetParam());
    std::map<Id, AABB> ref;
    std::mt19937_64 rng(99);
    for (Id id = 0; id < 300; ++id) { ref[id] = RandomBox(rng, 150.0, 4.0); idx->Insert(id, ref[id]); }

    for (int i = 0; i < 50; ++i) {
        const DVec3 c = RandomBox(rng, 150.0, 0.0).min;
        const double r = 5.0 + i;
        std::vector<Id> expect, got;
        for (const auto& [id, b] : ref) if (b.DistanceSquared(c) <= r * r) expect.push_back(id);
        idx->QuerySphere(c, r, got);
        EXPECT_EQ(got, expect);
    }
}

TEST_P(SpatialIndexTest, RayQueryIsSortedAndComplete) {
    auto idx = Make(GetParam());
    std::map<Id, AABB> ref;
    std::mt19937_64 rng(5);
    for (Id id = 0; id < 300; ++id) { ref[id] = RandomBox(rng, 150.0, 6.0); idx->Insert(id, ref[id]); }

    for (int i = 0; i < 60; ++i) {
        SpatialRay ray;
        ray.origin = RandomBox(rng, 150.0, 0.0).min;
        ray.direction = glm::normalize(RandomBox(rng, 1.0, 0.0).min + DVec3(0.001));
        if (i % 3 == 0) ray.maxT = 100.0;
        if (i % 7 == 0) ray.direction.y = 0.0;      // parallel to a slab

        std::vector<SpatialRayHit> hits;
        idx->QueryRay(ray, hits);

        std::vector<SpatialRayHit> expect;
        for (const auto& [id, b] : ref) { double t; if (ray.Intersect(b, t)) expect.push_back({id, t}); }
        std::sort(expect.begin(), expect.end(), [](auto& a, auto& b) { return a.t != b.t ? a.t < b.t : a.id < b.id; });

        ASSERT_EQ(hits.size(), expect.size());
        for (size_t k = 0; k < hits.size(); ++k) {
            EXPECT_EQ(hits[k].id, expect[k].id);
            EXPECT_DOUBLE_EQ(hits[k].t, expect[k].t);
        }
    }
}

TEST_P(SpatialIndexTest, FrustumQueryFindsEverythingInside) {
    auto idx = Make(GetParam());
    std::map<Id, AABB> ref;
    std::mt19937_64 rng(77);
    for (Id id = 0; id < 300; ++id) { ref[id] = RandomBox(rng, 150.0, 3.0); idx->Insert(id, ref[id]); }

    const DMat4 view = glm::lookAt(DVec3(0, 0, 0), DVec3(0, 0, -1), DVec3(0, 1, 0));
    for (bool zeroToOne : {false, true}) {
        const DMat4 proj = zeroToOne ? glm::perspectiveRH_ZO(glm::radians(70.0), 1.5, 0.1, 120.0)
                                     : glm::perspectiveRH_NO(glm::radians(70.0), 1.5, 0.1, 120.0);
        const SpatialFrustum f = SpatialFrustum::FromViewProjection(proj * view, zeroToOne);
        std::vector<Id> got, expect;
        for (const auto& [id, b] : ref) if (f.Intersects(b)) expect.push_back(id);
        idx->QueryFrustum(f, got);
        EXPECT_EQ(got, expect);
        EXPECT_FALSE(got.empty());
        EXPECT_LT(got.size(), ref.size());

        // A point straight ahead is inside, one behind the camera is not
        EXPECT_TRUE(f.Intersects(AABB({-0.1, -0.1, -50}, {0.1, 0.1, -49})));
        EXPECT_FALSE(f.Intersects(AABB({-0.1, -0.1, 49}, {0.1, 0.1, 50})));
    }
}

TEST_P(SpatialIndexTest, VisitorCanStopEarly) {
    auto idx = Make(GetParam());
    for (Id id = 0; id < 50; ++id) idx->Insert(id, AABB({double(id), 0, 0}, {id + 0.5, 1, 1}));
    int seen = 0;
    idx->QueryAABB(AABB({-1, -1, -1}, {100, 2, 2}), [&](Id, const AABB&) { return ++seen < 3; });
    EXPECT_EQ(seen, 3);
}

TEST_P(SpatialIndexTest, WorksVeryFarFromTheOrigin) {
    auto idx = Make(GetParam());
    const DVec3 far(1.0e12, -3.0e12, 5.0e11);
    idx->Insert(1, AABB(far, far + DVec3(1.0)));
    idx->Insert(2, AABB(far + DVec3(100.0), far + DVec3(101.0)));
    std::vector<Id> out;
    idx->QueryAABB(AABB(far - DVec3(1.0), far + DVec3(2.0)), out);
    EXPECT_EQ(out, (std::vector<Id>{1}));
    idx->QuerySphere(far + DVec3(100.5), 2.0, out);
    EXPECT_EQ(out, (std::vector<Id>{2}));
}

TEST(BVHIndex, StaysBalancedAndValid) {
    BVHIndex bvh(0.1);
    // Worst case for a naive tree : items inserted in a line
    for (Id id = 0; id < 2000; ++id) bvh.Insert(id, AABB({double(id), 0, 0}, {id + 0.5, 1, 1}));
    EXPECT_TRUE(bvh.Validate());
    EXPECT_LE(bvh.Height(), 24);        // log2(2000) ~ 11 ; generous for the heuristic

    std::mt19937_64 rng(3);
    for (int i = 0; i < 3000; ++i) {
        const Id id = rng() % 2000;
        if (i % 3 == 0) bvh.Remove(id);
        else bvh.Insert(id, RandomBox(rng, 500.0, 2.0));
    }
    EXPECT_TRUE(bvh.Validate());
}

TEST(GridIndex, HugeItemsGoToTheOversizedList) {
    GridIndex grid(1.0);
    grid.Insert(1, AABB({-1000, 0, -1000}, {1000, 0.1, 1000}));     // a ground plane
    grid.Insert(2, AABB({0, 0, 0}, {0.5, 0.5, 0.5}));
    EXPECT_EQ(grid.OversizedCount(), 1u);
    std::vector<Id> out;
    grid.QueryAABB(AABB({0, 0, 0}, {0.2, 0.2, 0.2}), out);
    EXPECT_EQ(out, (std::vector<Id>{1, 2}));
    grid.Remove(1);
    EXPECT_EQ(grid.OversizedCount(), 0u);
}

TEST(GridIndex, EmptyCellsAreFreed) {
    GridIndex grid(1.0);
    grid.Insert(1, AABB({0, 0, 0}, {5, 5, 5}));
    EXPECT_GT(grid.CellCount(), 1u);
    grid.Remove(1);
    EXPECT_EQ(grid.CellCount(), 0u);
}

TEST(OctreeIndex, FreesEmptyNodesAndKeepsOutsideItems) {
    OctreeIndex tree(DVec3(0.0), 100.0, 5);
    tree.Insert(1, AABB({1, 1, 1}, {1.5, 1.5, 1.5}));
    tree.Insert(2, AABB({5000, 0, 0}, {5001, 1, 1}));               // centre outside the cube
    EXPECT_GT(tree.NodeCount(), 1u);
    EXPECT_EQ(tree.OutsideCount(), 1u);
    tree.Remove(1);
    tree.Remove(2);
    EXPECT_EQ(tree.NodeCount(), 1u);                                // only the root remains
    EXPECT_EQ(tree.OutsideCount(), 0u);
}

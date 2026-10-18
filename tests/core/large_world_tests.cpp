#include <gtest/gtest.h>

#include "engine/core/math/large_world.hpp"
#include "engine/core/math/reference_frame.hpp"

using namespace Shard::Engine::Core;

namespace {
    constexpr double kEps = 1e-9;

    bool Near(const DVec3& a, const DVec3& b, double eps = kEps) { return glm::length(a - b) <= eps; }
}

TEST(WorldPosition, NormalizesIntoItsCell) {
    WorldPosition p(CellCoord{}, DVec3(2500.0, -10.0, 1023.5));
    EXPECT_EQ(p.cell.x, 2);
    EXPECT_EQ(p.cell.y, -1);
    EXPECT_EQ(p.cell.z, 0);
    EXPECT_NEAR(p.local.x, 452.0, kEps);
    EXPECT_NEAR(p.local.y, 1014.0, kEps);
    EXPECT_NEAR(p.local.z, 1023.5, kEps);
    EXPECT_TRUE(Near(p.ToDVec3(), DVec3(2500.0, -10.0, 1023.5)));
}

TEST(WorldPosition, CarriesCellsWhenMoved) {
    WorldPosition p(CellCoord{5, 0, 0}, DVec3(1000.0, 0.0, 0.0));
    p += DVec3(100.0, 0.0, 0.0);
    EXPECT_EQ(p.cell.x, 6);
    EXPECT_NEAR(p.local.x, 76.0, kEps);
}

TEST(WorldPosition, OffsetStaysExactVeryFarFromTheOrigin) {
    // 10^15 cells * 1024 m = ~10^18 m : a double cannot even tell two positions a millimetre apart there
    const CellCoord far{1000000000000000ll, -1000000000000000ll, 7};
    const WorldPosition a(far, DVec3(10.001, 20.0, 30.0));
    const WorldPosition b(far, DVec3(10.0, 20.0, 31.5));

    const DVec3 d = a - b;
    EXPECT_NEAR(d.x, 0.001, 1e-12);
    EXPECT_NEAR(d.y, 0.0, 1e-12);
    EXPECT_NEAR(d.z, -1.5, 1e-12);

    // The same two positions as plain doubles collapse
    EXPECT_NE(a.ToDVec3().x - b.ToDVec3().x, 0.001);
}

TEST(WorldPosition, RelativeToIsAFloatOffset) {
    const WorldPosition camera(CellCoord{123456789, 0, 0}, DVec3(100.0, 5.0, 5.0));
    const WorldPosition object = camera + DVec3(3.0, 1.0, -2.0);
    const Vec3 rel = object.RelativeTo(camera);
    EXPECT_FLOAT_EQ(rel.x, 3.0f);
    EXPECT_FLOAT_EQ(rel.y, 1.0f);
    EXPECT_FLOAT_EQ(rel.z, -2.0f);
}

TEST(ReferenceFrames, RootAlwaysExists) {
    ReferenceFrameTree tree;
    EXPECT_TRUE(tree.Exists(kRootFrame));
    EXPECT_EQ(tree.Count(), 1u);
    EXPECT_FALSE(tree.Remove(kRootFrame));
    EXPECT_FALSE(tree.SetOrigin(kRootFrame, WorldPosition()));
    EXPECT_EQ(tree.Create("orphan", 99), kInvalidFrame);
}

TEST(ReferenceFrames, PointToRootThroughNestedFrames) {
    ReferenceFrameTree tree;
    const FrameId planet = tree.Create("Planet", kRootFrame, WorldPosition::FromDVec3(DVec3(1000.0, 0.0, 0.0)));
    const FrameId ship = tree.Create("Ship", planet, WorldPosition::FromDVec3(DVec3(0.0, 50.0, 0.0)));

    EXPECT_TRUE(Near(tree.ToRoot(ship, DVec3(1.0, 2.0, 3.0)).ToDVec3(), DVec3(1001.0, 52.0, 3.0)));
    EXPECT_TRUE(Near(tree.FromRoot(ship, WorldPosition::FromDVec3(DVec3(1001.0, 52.0, 3.0))), DVec3(1.0, 2.0, 3.0)));
    EXPECT_EQ(tree.Depth(ship), 2);
}

TEST(ReferenceFrames, RotationIsApplied) {
    ReferenceFrameTree tree;
    // A frame turned a quarter turn around Y : its +X axis points to the root's -Z
    const DQuat quarter = glm::angleAxis(Math::kHalfPi, DVec3(0.0, 1.0, 0.0));
    const FrameId f = tree.Create("Turned", kRootFrame, WorldPosition::FromDVec3(DVec3(10.0, 0.0, 0.0)), quarter);

    EXPECT_TRUE(Near(tree.ToRoot(f, DVec3(1.0, 0.0, 0.0)).ToDVec3(), DVec3(10.0, 0.0, -1.0), 1e-9));
    EXPECT_TRUE(Near(tree.ConvertDirection(f, kRootFrame, DVec3(1.0, 0.0, 0.0)), DVec3(0.0, 0.0, -1.0), 1e-9));

    const DVec3 p(3.0, -2.0, 5.0);
    EXPECT_TRUE(Near(tree.FromRoot(f, tree.ToRoot(f, p)), p, 1e-9));
}

TEST(ReferenceFrames, ConvertBetweenSiblingsKeepsThePrecisionOfTheirDistance) {
    ReferenceFrameTree tree;
    // Two frames a few metres apart, both ~10^17 m from the root
    const CellCoord far{100000000000000ll, 0, 0};
    const FrameId a = tree.Create("A", kRootFrame, WorldPosition(far, DVec3(0.0, 0.0, 0.0)));
    const FrameId b = tree.Create("B", kRootFrame, WorldPosition(far, DVec3(4.0, 0.0, 0.0)));

    // The point (0.001, 0, 0) of A is the point (-3.999, 0, 0) of B
    const DVec3 q = tree.ConvertPoint(a, b, DVec3(0.001, 0.0, 0.0));
    EXPECT_NEAR(q.x, -3.999, 1e-9);
    EXPECT_NEAR(q.y, 0.0, 1e-9);

    const DVec3 back = tree.ConvertPoint(b, a, q);
    EXPECT_NEAR(back.x, 0.001, 1e-9);
}

TEST(ReferenceFrames, ConvertToAncestorAndDescendant) {
    ReferenceFrameTree tree;
    const FrameId planet = tree.Create("Planet", kRootFrame, WorldPosition::FromDVec3(DVec3(100.0, 0.0, 0.0)));
    const FrameId ship = tree.Create("Ship", planet, WorldPosition::FromDVec3(DVec3(0.0, 10.0, 0.0)));

    EXPECT_TRUE(Near(tree.ConvertPoint(ship, planet, DVec3(1.0, 1.0, 1.0)), DVec3(1.0, 11.0, 1.0)));
    EXPECT_TRUE(Near(tree.ConvertPoint(planet, ship, DVec3(1.0, 11.0, 1.0)), DVec3(1.0, 1.0, 1.0)));
    EXPECT_EQ(tree.CommonAncestor(ship, planet), planet);
    EXPECT_EQ(tree.CommonAncestor(ship, kRootFrame), kRootFrame);
}

TEST(ReferenceFrames, RelativePoseOfAFrame) {
    ReferenceFrameTree tree;
    const DQuat q = glm::angleAxis(0.5, glm::normalize(DVec3(1.0, 2.0, 3.0)));
    const FrameId a = tree.Create("A", kRootFrame, WorldPosition::FromDVec3(DVec3(5.0, 0.0, 0.0)), q);
    const FrameId b = tree.Create("B", a, WorldPosition::FromDVec3(DVec3(0.0, 2.0, 0.0)));

    const RelativePose pose = tree.GetRelativePose(b, kRootFrame);
    const DVec3 p(1.0, 2.0, 3.0);
    EXPECT_TRUE(Near(pose.TransformPoint(p), tree.ConvertPoint(b, kRootFrame, p), 1e-9));
    EXPECT_TRUE(tree.GetRelativePose(a, a).IsIdentity());
}

TEST(ReferenceFrames, SetParentKeepsTheWorldPose) {
    ReferenceFrameTree tree;
    const DQuat q = glm::angleAxis(1.0, DVec3(0.0, 1.0, 0.0));
    const FrameId a = tree.Create("A", kRootFrame, WorldPosition::FromDVec3(DVec3(10.0, 0.0, 0.0)), q);
    const FrameId b = tree.Create("B", kRootFrame, WorldPosition::FromDVec3(DVec3(0.0, 0.0, 30.0)));

    const DVec3 before = tree.ToRoot(b, DVec3(1.0, 2.0, 3.0)).ToDVec3();
    ASSERT_TRUE(tree.SetParent(b, a, true));
    EXPECT_EQ(tree.Find(b)->parent, a);
    EXPECT_TRUE(Near(tree.ToRoot(b, DVec3(1.0, 2.0, 3.0)).ToDVec3(), before, 1e-9));
}

TEST(ReferenceFrames, CannotMakeAFrameItsOwnAncestor) {
    ReferenceFrameTree tree;
    const FrameId a = tree.Create("A");
    const FrameId b = tree.Create("B", a);
    EXPECT_FALSE(tree.SetParent(a, b));
    EXPECT_FALSE(tree.SetParent(a, a));
    EXPECT_EQ(tree.Find(a)->parent, kRootFrame);
}

TEST(ReferenceFrames, RemoveKeepsChildrenWhereTheyAre) {
    ReferenceFrameTree tree;
    const FrameId a = tree.Create("A", kRootFrame, WorldPosition::FromDVec3(DVec3(100.0, 0.0, 0.0)));
    const FrameId b = tree.Create("B", a, WorldPosition::FromDVec3(DVec3(0.0, 5.0, 0.0)));
    const DVec3 before = tree.ToRoot(b, DVec3(1.0, 0.0, 0.0)).ToDVec3();

    ASSERT_TRUE(tree.Remove(a));
    EXPECT_FALSE(tree.Exists(a));
    EXPECT_EQ(tree.Find(b)->parent, kRootFrame);
    EXPECT_TRUE(Near(tree.ToRoot(b, DVec3(1.0, 0.0, 0.0)).ToDVec3(), before));
}

TEST(ReferenceFrames, VersionChangesWithTheTree) {
    ReferenceFrameTree tree;
    const uint64_t v0 = tree.GetVersion();
    const FrameId a = tree.Create("A");
    EXPECT_GT(tree.GetVersion(), v0);
    const uint64_t v1 = tree.GetVersion();
    tree.SetOrigin(a, WorldPosition::FromDVec3(DVec3(1.0)));
    EXPECT_GT(tree.GetVersion(), v1);
}

TEST(ReferenceFrames, CreateWithIdKeepsTheNextIdAhead) {
    ReferenceFrameTree tree;
    ASSERT_TRUE(tree.CreateWithId(7, "Seven", kRootFrame, WorldPosition(), DQuat(1.0, 0.0, 0.0, 0.0)));
    EXPECT_FALSE(tree.CreateWithId(7, "Again", kRootFrame, WorldPosition(), DQuat(1.0, 0.0, 0.0, 0.0)));
    const FrameId next = tree.Create("Next");
    EXPECT_GT(next, 7u);
}

TEST(WorldPosition, ExtremePositionsDoNotOverflow) {
    // A float position near FLT_MAX is ~10^35 cells : beyond int64. Pinned, finite, no undefined conversion
    const WorldPosition huge = WorldPosition::FromDVec3(DVec3(3.0e38, -3.0e38, 0.0));
    EXPECT_TRUE(std::isfinite(glm::length(huge.ToDVec3())));
    EXPECT_GT(huge.cell.x, 0);
    EXPECT_LT(huge.cell.y, 0);

    const WorldPosition inf = WorldPosition::FromDVec3(DVec3(INFINITY, NAN, 1.0));
    EXPECT_TRUE(std::isfinite(glm::length(inf.ToDVec3())));

    // The offset to a sane position is finite, so a renderer can keep working
    EXPECT_TRUE(std::isfinite(glm::length(WorldPosition::Offset(huge, WorldPosition()))));
}

TEST(WorldPosition, PastTheCellsReachItDegradesToADouble) {
    // 1e30 m is ~10^27 cells : past int64. The value is still there (not pinned to the edge), and differences stay finite
    const WorldPosition a = WorldPosition::FromDVec3(DVec3(1.0e30, 0.0, 0.0));
    EXPECT_FALSE(a.InCellRange());
    EXPECT_NEAR(a.ToDVec3().x / 1.0e30, 1.0, 1e-12);
    EXPECT_EQ(WorldPosition::Offset(a, a), DVec3(0.0));
    EXPECT_NEAR(WorldPosition::Offset(a, WorldPosition()).x / 1.0e30, 1.0, 1e-12);
}

TEST(WorldPosition, NormalisingASaturatedPositionAgainChangesNothing) {
    // Every operation normalises : a position past the cells must be a fixed point, or its cell grows by 4e18 each time and wraps
    const WorldPosition a = WorldPosition::FromDVec3(DVec3(1.0e30, 5.0, 0.0));
    WorldPosition b = a;
    for (int i = 0; i < 8; i++)
        b = WorldPosition(b.cell, b.local);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a + DVec3(0.0), a);
    EXPECT_EQ(WorldPosition::Offset(a + DVec3(1.0, 0.0, 0.0), a).y, 0.0);
}

TEST(ReferenceFrames, AFrameAtOneE30HoldsItsObject) {
    ReferenceFrameTree tree;
    const WorldPosition origin = WorldPosition::FromDVec3(DVec3(1.0e30, 0.0, 0.0));
    const FrameId f = tree.Create("far", kRootFrame, origin);
    EXPECT_EQ(tree.GetOriginInRoot(f), origin);
    const WorldPosition p = tree.ToRoot(f, DVec3(0.0, 4.5, 1.0));
    EXPECT_EQ(tree.FromRoot(f, p), DVec3(0.0, 4.5, 1.0));
}

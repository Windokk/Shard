#include <gtest/gtest.h>

#include <cstring>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/renderer/components/render_space.hpp"

using namespace Shard::Engine::Core;
using namespace Shard::Engine::Rendering;

TEST(RenderSpace, RootFrameAtTheOriginIsTheIdentityBitForBit) {
    ReferenceFrameTree tree;
    const FrameToRender space = MakeFrameToRender(tree, kRootFrame, WorldPosition());
    EXPECT_TRUE(space.identity);

    const glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(1.1f, 2.2f, 3.3f)) * glm::mat4_cast(glm::quat(glm::vec3(0.3f, 0.2f, 0.1f)));
    const glm::mat4 out = space.ApplyToMatrix(m);
    EXPECT_EQ(0, std::memcmp(&m, &out, sizeof(m)));

    const glm::vec3 p(0.1f, 0.2f, 0.3f);
    EXPECT_EQ(space.ApplyToPoint(p), p);
}

TEST(RenderSpace, ObjectNextToTheCameraIsExactWhateverTheDistanceFromTheRoot) {
    ReferenceFrameTree tree;
    // A frame 10^16 m from the root (cells), the camera in it, an object one metre from the camera
    const CellCoord far{10000000000000ll, 0, -5000000000000ll};
    const FrameId planet = tree.Create("Planet", kRootFrame, WorldPosition(far, DVec3(0.0)));

    const WorldPosition camera = tree.ToRoot(planet, DVec3(100.5, 20.0, -3.0));
    const FrameToRender space = MakeFrameToRender(tree, planet, camera);
    EXPECT_FALSE(space.identity);

    const glm::vec3 cameraLocal(100.5f, 20.0f, -3.0f);
    const glm::vec3 objectLocal = cameraLocal + glm::vec3(1.0f, 0.0f, 0.0f);

    EXPECT_EQ(space.ApplyToPoint(cameraLocal), glm::vec3(0.0f));
    const glm::vec3 rel = space.ApplyToPoint(objectLocal);
    EXPECT_FLOAT_EQ(rel.x, 1.0f);
    EXPECT_FLOAT_EQ(rel.y, 0.0f);
    EXPECT_FLOAT_EQ(rel.z, 0.0f);

    const glm::mat4 world = glm::translate(glm::mat4(1.0f), objectLocal);
    EXPECT_FLOAT_EQ(space.ApplyToMatrix(world)[3].x, 1.0f);
}

TEST(RenderSpace, RenderOriginMovesRootObjects) {
    ReferenceFrameTree tree;
    const WorldPosition origin = WorldPosition::FromDVec3(DVec3(5000.0, 0.0, 0.0));
    const FrameToRender space = MakeFrameToRender(tree, kRootFrame, origin);
    EXPECT_FALSE(space.identity);
    EXPECT_FLOAT_EQ(space.ApplyToPoint(glm::vec3(5001.0f, 2.0f, 0.0f)).x, 1.0f);
}

TEST(RenderSpace, ARotatedFrameTurnsPointsAndDirections) {
    ReferenceFrameTree tree;
    const FrameId f = tree.Create("Turned", kRootFrame, WorldPosition::FromDVec3(DVec3(10.0, 0.0, 0.0)),
                                  glm::angleAxis(Math::kHalfPi, DVec3(0.0, 1.0, 0.0)));
    const FrameToRender space = MakeFrameToRender(tree, f, WorldPosition());

    const glm::vec3 p = space.ApplyToPoint(glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_NEAR(p.x, 10.0f, 1e-5f);
    EXPECT_NEAR(p.z, -1.0f, 1e-5f);

    const glm::vec3 d = space.ApplyToDirection(glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_NEAR(d.z, -1.0f, 1e-5f);

    // The matrix and the point agree
    const glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 viaMatrix = glm::vec3(space.ApplyToMatrix(m)[3]);
    EXPECT_NEAR(viaMatrix.x, p.x, 1e-5f);
    EXPECT_NEAR(viaMatrix.z, p.z, 1e-5f);
}

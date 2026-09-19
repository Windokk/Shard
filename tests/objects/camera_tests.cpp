#include <gtest/gtest.h>

#include <cmath>

#include "support/test_engine_context.hpp"

#include "engine/objects/actors/actor.hpp"
#include "engine/objects/components/rendering/camera.hpp"

using namespace Shard::Engine::Core;
using namespace Shard::Engine::Objects;
using namespace Shard::Engine::Objects::Components;

class CameraTest : public ::testing::Test {
    protected:
        void SetUp() override {
            SetEngine(&context);
            actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);
            camera = actor->AddComponent<Camera>();
        }

        void TearDown() override {
            camera.reset();
            actor.reset();
            SetEngine(nullptr);
        }

        // Depth (0 = near plane, 1 = far plane) a point straight ahead of the camera ends up at
        float DepthAtDistance(float distance) {
            glm::vec4 clip = camera->GetProjection() * glm::vec4(0.0f, 0.0f, -distance, 1.0f);
            return (clip.z / clip.w) * 0.5f + 0.5f;
        }

        Shard::Tests::TestEngineContext context;
        std::shared_ptr<Actor> actor;
        std::shared_ptr<Camera> camera;
};

TEST_F(CameraTest, DefaultPlanesFormAValidPerspectiveRange) {
    EXPECT_GT(camera->nearPlane, 0.0f);
    EXPECT_GT(camera->farPlane, camera->nearPlane);
}

TEST_F(CameraTest, ZeroNearPlaneIsClampedSoDepthIsNotCollapsed) {
    // What a camera added from the editor and only given a far plane looks like
    camera->Init(1280, 720, 0.0f, 110.0f, 60.0f, false, 10.0f);
    camera->UpdateMatrix();

    EXPECT_GE(camera->nearPlane, Camera::MIN_NEAR_PLANE);

    // With near == 0 every point maps to depth 1, so nothing could be told apart from the far plane
    float nearDepth = DepthAtDistance(1.0f);
    float farDepth = DepthAtDistance(50.0f);

    EXPECT_TRUE(std::isfinite(nearDepth));
    EXPECT_LT(nearDepth, farDepth);
    EXPECT_LT(farDepth, 1.0f);
}

TEST_F(CameraTest, NegativeNearPlaneIsClamped) {
    camera->Init(1280, 720, -5.0f, 100.0f, 60.0f, false, 10.0f);

    EXPECT_GE(camera->nearPlane, Camera::MIN_NEAR_PLANE);
}

TEST_F(CameraTest, FarPlaneAlwaysStaysBeyondNearPlane) {
    camera->Init(1280, 720, 0.5f, 0.0f, 60.0f, false, 10.0f);
    camera->UpdateMatrix();

    EXPECT_GT(camera->farPlane, camera->nearPlane);
}

TEST_F(CameraTest, EditedPlanesAreSanitizedOnNextMatrixUpdate) {
    camera->nearPlane = 0.0f; // e.g. typed into the properties panel
    camera->UpdateMatrix();

    EXPECT_GE(camera->nearPlane, Camera::MIN_NEAR_PLANE);
    EXPECT_LT(DepthAtDistance(10.0f), 1.0f);
}

TEST_F(CameraTest, ValidPlanesAreLeftUntouched) {
    camera->Init(1280, 720, 0.3f, 250.0f, 60.0f, false, 10.0f);
    camera->UpdateMatrix();

    EXPECT_FLOAT_EQ(camera->nearPlane, 0.3f);
    EXPECT_FLOAT_EQ(camera->farPlane, 250.0f);
}

TEST_F(CameraTest, OrthographicCameraKeepsItsNearPlane) {
    camera->Init(1280, 720, 0.0f, 100.0f, 60.0f, true, 10.0f);
    camera->UpdateMatrix();

    EXPECT_FLOAT_EQ(camera->nearPlane, 0.0f);
}

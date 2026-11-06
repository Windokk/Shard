#include <gtest/gtest.h>

#include <cmath>

#include "support/test_engine_context.hpp"

#include "engine/world/actor.hpp"
#include "engine/renderer/components/camera.hpp"

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

        float DepthAtDistance(float distance) {
            glm::vec4 clip = camera->GetProjection() * glm::vec4(0.0f, 0.0f, -distance, 1.0f);
            return (clip.z / clip.w) * 0.5f + 0.5f;
        }

        Shard::Tests::TestEngineContext context;
        std::shared_ptr<Actor> actor;
        std::shared_ptr<Camera> camera;
};

TEST_F(CameraTest, DefaultPlanesAreValid) {
    EXPECT_GT(camera->nearPlane, 0.0f);
    EXPECT_GT(camera->farPlane, camera->nearPlane);
}

TEST_F(CameraTest, ZeroNearPlaneClamped) {
    camera->Init(1280, 720, 0.0f, 110.0f, 60.0f, false, 10.0f);
    camera->UpdateMatrix();

    EXPECT_GE(camera->nearPlane, Camera::MIN_NEAR_PLANE);

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

TEST_F(CameraTest, FarStaysBeyondNear) {
    camera->Init(1280, 720, 0.5f, 0.0f, 60.0f, false, 10.0f);
    camera->UpdateMatrix();

    EXPECT_GT(camera->farPlane, camera->nearPlane);
}

TEST_F(CameraTest, EditedPlanesSanitized) {
    camera->nearPlane = 0.0f;
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

TEST_F(CameraTest, OrthographicKeepsNearPlane) {
    camera->Init(1280, 720, 0.0f, 100.0f, 60.0f, true, 10.0f);
    camera->UpdateMatrix();

    EXPECT_FLOAT_EQ(camera->nearPlane, 0.0f);
}

#include <gtest/gtest.h>

#include "support/test_engine_context.hpp"

#include "engine/world/actor.hpp"
#include "engine/world/components/transform.hpp"
#include "engine/renderer/components/camera.hpp"

using namespace Shard::Engine::Core;
using namespace Shard::Engine::Objects;
using namespace Shard::Engine::Objects::Components;

class ComponentSerializationTest : public ::testing::Test {
    protected:
        void SetUp() override {
            SetEngine(&context);
            actor = Object::CreateWithContext<Actor>(&context, std::string("Subject"), &context);
        }

        void TearDown() override {
            actor.reset();
            SetEngine(nullptr);
        }

        Shard::Tests::TestEngineContext context;
        std::shared_ptr<Actor> actor;
};

TEST_F(ComponentSerializationTest, TransformIsWrittenFromItsReflectedFields) {
    actor->transform->SetPosition(glm::vec3(1, 2, 3));
    actor->transform->SetScale(glm::vec3(2, 2, 2));

    ordered_json out = actor->transform->Serialize();

    EXPECT_EQ(out["type"], "transform");
    EXPECT_TRUE(out["active"]);
    EXPECT_FLOAT_EQ(out["position"]["z"], 3.0f);
    EXPECT_FLOAT_EQ(out["scale"]["x"], 2.0f);
    EXPECT_TRUE(out["rotation"].contains("y"));
}

TEST_F(ComponentSerializationTest, TransformRoundTrips) {
    actor->transform->SetPosition(glm::vec3(-4, 0.5f, 9));
    actor->transform->SetRotation(glm::vec3(0, 90, 0));
    actor->transform->SetScale(glm::vec3(1, 3, 1));

    const json saved = actor->transform->Serialize();

    auto other = Object::CreateWithContext<Actor>(&context, std::string("Other"), &context);
    other->transform->Deserialize(saved);

    EXPECT_EQ(other->transform->position, actor->transform->position);
    EXPECT_EQ(other->transform->scale, actor->transform->scale);
    EXPECT_NEAR(other->transform->GetRotation().y, 90.0f, 0.1f);
}

TEST_F(ComponentSerializationTest, TransformReadsTheFormatLevelsAlwaysUsed) {
    const json legacy = json::parse(R"({
        "type": "transform", "active": true,
        "position": {"x": 1.0, "y": 2.0, "z": 3.0},
        "rotation": {"x": 0.0, "y": 45.0, "z": 0.0},
        "scale":    {"x": 2.0, "y": 2.0, "z": 2.0}
    })");

    actor->transform->Deserialize(legacy);

    EXPECT_EQ(actor->transform->position, glm::vec3(1, 2, 3));
    EXPECT_EQ(actor->transform->scale, glm::vec3(2));
    EXPECT_NEAR(actor->transform->GetRotation().y, 45.0f, 1e-3f);
}

TEST_F(ComponentSerializationTest, TransformKeepsItsDefaultsForMissingKeys) {
    actor->transform->Deserialize(json::parse(R"({"type": "transform"})"));

    EXPECT_EQ(actor->transform->position, glm::vec3(0));
    EXPECT_EQ(actor->transform->scale, glm::vec3(1));
}

TEST_F(ComponentSerializationTest, CameraIsWrittenFromItsReflectedFields) {
    auto camera = actor->AddComponent<Camera>();
    camera->Init(1280, 720, 0.5f, 250.0f, 75.0f, true, 4.0f);

    ordered_json out = camera->Serialize();

    EXPECT_EQ(out["type"], "camera");
    EXPECT_FLOAT_EQ(out["nearPlane"], 0.5f);
    EXPECT_FLOAT_EQ(out["farPlane"], 250.0f);
    EXPECT_FLOAT_EQ(out["fov"], 75.0f);
    EXPECT_TRUE(out["orthographic"]);
    EXPECT_FLOAT_EQ(out["orthoSize"], 4.0f);
}

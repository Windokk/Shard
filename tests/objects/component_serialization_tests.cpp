#include <gtest/gtest.h>

#include "support/test_engine_context.hpp"

#include "engine/world/actor.hpp"
#include "engine/world/components/transform.hpp"
#include "engine/renderer/components/camera.hpp"
#include "engine/renderer/components/light_component.hpp"
#include "engine/renderer/components/model_component.hpp"

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

// ---- Baked lighting ----

TEST_F(ComponentSerializationTest, LightKeepsItsSoftShadowSizeAndBakeMode) {
    auto light = actor->AddComponent<Light>();
    light->SetSourceSize(0.35f);
    light->SetBakeMode(Shard::Engine::Rendering::LightBakeMode::Static);

    ordered_json out = light->Serialize();
    EXPECT_EQ(out["type"], "light");
    EXPECT_FLOAT_EQ(out["sourceSize"], 0.35f);
    EXPECT_EQ(out["bakeMode"], "Static");

    // What the renderer reads is the light's data, which the setters feed
    EXPECT_FLOAT_EQ(light->GetData().sourceSize, 0.35f);
    EXPECT_EQ(light->GetData().bakeMode, int(Shard::Engine::Rendering::LightBakeMode::Static));

    auto other = actor->AddComponent<Light>();
    other->Deserialize(json(out));
    EXPECT_FLOAT_EQ(other->GetData().sourceSize, 0.35f);
    EXPECT_EQ(other->GetData().bakeMode, int(Shard::Engine::Rendering::LightBakeMode::Static));
}

TEST_F(ComponentSerializationTest, ALightWithoutTheBakingKeysIsADynamicHardShadowedOne) {
    // Every level written before baking existed : no sourceSize, no bakeMode
    auto light = actor->AddComponent<Light>();
    light->Deserialize(json::parse(R"({"type": "light", "active": true, "lightType": "Point", "intensity": 3.0, "radius": 10.0})"));

    EXPECT_FLOAT_EQ(light->GetData().sourceSize, 0.0f);
    EXPECT_EQ(light->GetData().bakeMode, int(Shard::Engine::Rendering::LightBakeMode::Dynamic));
}

TEST_F(ComponentSerializationTest, ModelTakesPartInTheBakedLightingUnlessItSaysOtherwise) {
    // (Serializing a model reaches the asset manager, which this context does not have : the file format of the placement is
    // checked by the sample world round trip, see lightmaps.world)
    auto model = actor->AddComponent<Model>();

    EXPECT_TRUE(model->staticLighting);
    EXPECT_EQ(model->lightmapResolution, 0);
    EXPECT_EQ(model->lightmapPage, -1) << "no lightmap until a bake gives it one";

    model->lightmapPage = 2;
    model->ClearLightmap();
    EXPECT_EQ(model->lightmapPage, -1);
}

TEST_F(ComponentSerializationTest, ACopyOfAModelDoesNotShareItsLightmap) {
    auto model = actor->AddComponent<Model>();
    model->lightmapPage = 1;

    auto copy = std::dynamic_pointer_cast<Model>(model->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->lightmapPage, -1) << "the copy stands somewhere else : the original's lightmap would be the wrong light";
    EXPECT_EQ(model->lightmapPage, 1);
}

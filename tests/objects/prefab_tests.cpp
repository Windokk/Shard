#include <gtest/gtest.h>

#include "support/test_engine_context.hpp"

#include "engine/world/actor.hpp"
#include "engine/world/prefab.hpp"
#include "engine/world/world.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Core;
using namespace Shard::Engine::Objects;
using nlohmann::json;

namespace {
    json TransformJson(float x, float y, float z)
    {
        return json{ { "type", "transform" }, { "active", true },
                     { "position", { { "x", x }, { "y", y }, { "z", z } } },
                     { "rotation", { { "x", 0.0 }, { "y", 0.0 }, { "z", 0.0 } } },
                     { "scale", { { "x", 1.0 }, { "y", 1.0 }, { "z", 1.0 } } } };
    }

    json CrateJson()
    {
        return json{ { "name", "crate" }, { "components", json::array({ TransformJson(0, 0, 0) }) },
                     { "children", json::array({ json{ { "name", "lid" }, { "components", json::array({ TransformJson(0, 1, 0) }) } } }) } };
    }
}

class PrefabTest : public ::testing::Test {
    protected:
        void SetUp() override {
            SetEngine(&context);
            previousReader = Worlds::GetPrefabProjectReader();
            Worlds::SetPrefabProjectReader([](const std::string& name, std::string& text, std::string& canonical) {
                if (name != "crate" && name != "prefabs/crate.prefab")
                    return false;
                text = CrateJson().dump();
                canonical = "prefabs/crate.prefab";
                return true;
            });
            world = std::make_unique<Worlds::World>("w", Filesystem::Path("w.world"));
        }

        void TearDown() override {
            world.reset();
            Worlds::SetPrefabProjectReader(previousReader);
            SetEngine(nullptr);
        }

        Worlds::PrefabContentReader previousReader;
        Shard::Tests::TestEngineContext context;
        std::unique_ptr<Worlds::World> world;
};

TEST_F(PrefabTest, DiffThenApplyGivesTheModifiedActor) {
    json base = CrateJson();
    json modified = CrateJson();
    modified["components"][0]["position"]["x"] = 5.0;
    modified["children"][0]["components"][0]["scale"]["y"] = 2.0;
    modified["children"].push_back(json{ { "name", "extra" }, { "components", json::array({ TransformJson(1, 1, 1) }) } });

    const json diff = Worlds::DiffPrefabActor(base, modified);
    json rebuilt = base;
    Worlds::ApplyPrefabDiff(rebuilt, diff);

    EXPECT_EQ(rebuilt, modified);
    EXPECT_TRUE(Worlds::DiffPrefabActor(base, base).empty());
}

TEST_F(PrefabTest, PrefabStemDropsFolderAndExtension) {
    EXPECT_EQ(Worlds::PrefabStem("prefabs/props/crate.prefab"), "crate");
    EXPECT_EQ(Worlds::PrefabStem("crate"), "crate");
}

TEST_F(PrefabTest, SpawnedInstancesAreFoundByNameAndDestroyedWhole) {
    auto first = world->SpawnPrefab("crate");
    auto second = world->SpawnPrefab("crate", nullptr, json::object(), "crate_b");
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(second->GetName(), "crate_b");
    EXPECT_FALSE(world->SpawnPrefab("missing"));

    EXPECT_EQ(world->FindPrefabInstances("crate").size(), 2u);
    EXPECT_EQ(world->FindPrefabInstances("prefabs/crate.prefab").size(), 2u);
    EXPECT_TRUE(world->FindPrefabInstances("barrel").empty());

    auto lid = std::dynamic_pointer_cast<Actor>(first->GetChild(0));
    ASSERT_TRUE(lid);
    EXPECT_EQ(lid->GetPrefabRoot(), first);

    first->Destroy();
    const auto remaining = world->FindPrefabInstances("crate");
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining[0], second);

    EXPECT_EQ(world->DestroyPrefabInstances("crate"), 1u);
    EXPECT_TRUE(world->FindPrefabInstances("crate").empty());
}

TEST_F(PrefabTest, OverridesPlaceTheInstanceBeforeItsChildrenAreBuilt) {
    float position[3] = { 4.f, 5.f, 6.f };
    float rotation[4] = { 0.f, 0.f, 0.f, 1.f };
    float scale[3] = { 1.f, 1.f, 1.f };
    auto crate = world->SpawnPrefab("crate", nullptr, Worlds::MakeTransformOverride(position, rotation, scale));
    ASSERT_TRUE(crate);
    EXPECT_FLOAT_EQ(crate->transform->GetPosition().x, 4.f);
    EXPECT_FLOAT_EQ(crate->transform->GetPosition().y, 5.f);
}

TEST_F(PrefabTest, InstanceIsSavedAsALinkAndComesBack) {
    json overrides = { { "components", { { "transform", { { "position", { { "x", 7.0 } } } } } } } };
    auto crate = world->SpawnPrefab("crate", nullptr, overrides);
    ASSERT_TRUE(crate);

    const auto saved = Worlds::SerializeActorToJson(crate);
    EXPECT_EQ(saved["prefab"], "prefabs/crate.prefab");
    EXPECT_FALSE(saved.contains("components"));
    EXPECT_FALSE(saved.contains("children"));
    EXPECT_DOUBLE_EQ(saved["overrides"]["components"]["transform"]["position"]["x"].get<double>(), 7.0);

    auto restored = Worlds::InstantiateActor(*world, json::parse(saved.dump()));
    ASSERT_TRUE(restored);
    EXPECT_FLOAT_EQ(restored->transform->GetPosition().x, 7.f);
    EXPECT_EQ(restored->GetChildrenCount(), 1);
    EXPECT_EQ(world->FindPrefabInstances("crate").size(), 2u);
}

TEST_F(PrefabTest, MissingPrefabIsKeptAsItIsInTheFile) {
    const json link = { { "name", "ghost" }, { "prefab", "prefabs/gone.prefab" }, { "overrides", { { "folder", "x" } } } };
    auto ghost = Worlds::InstantiateActor(*world, link);
    ASSERT_TRUE(ghost);
    EXPECT_EQ(json::parse(Worlds::SerializeActorToJson(ghost).dump()), link);
}

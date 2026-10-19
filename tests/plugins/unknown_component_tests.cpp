#include <gtest/gtest.h>

#include "plugins/plugin_test_utils.hpp"
#include "support/test_engine_context.hpp"

#include "engine/world/actor.hpp"
#include "engine/world/world.hpp"

using namespace Shard::Engine;
using nlohmann::json;
using PluginTest::TempDir;

// Opening a world whose plugin is not loaded, then saving it, must not cost the plugin's data.
class UnknownComponentTest : public ::testing::Test {
protected:
    void SetUp() override { Core::SetEngine(&context); }
    void TearDown() override { Core::SetEngine(nullptr); }
    Shard::Tests::TestEngineContext context;
};

TEST_F(UnknownComponentTest, AComponentOfAMissingPluginSurvivesALoadAndASave)
{
    TempDir dir;
    json world = json::parse(R"({
        "name": "mods",
        "actors": [
            { "name": "Statue", "components": [
                { "type": "transform", "active": true, "position": { "x": 1.0, "y": 2.0, "z": 3.0 } },
                { "type": "com.acme.weather/Rain", "intensity": 0.75, "tags": ["wet", "cold"], "nested": { "a": 1 } } ] } ],
        "settings": {} })");
    dir.Write("in.world", world.dump());

    Worlds::World loaded("mods", Filesystem::Path(dir.Sub("in.world")));
    loaded.Deserialize(Filesystem::Path(dir.Sub("in.world")));
    auto roots = loaded.GetRootActors();
    ASSERT_EQ(roots.size(), 1u);
    ASSERT_EQ(roots.begin()->second->GetUnknownComponents().size(), 1u);

    // What World::Serialize writes for an actor
    const json saved = json::object({ { "actors", json::array({ json(Worlds::SerializeActorToJson(roots.begin()->second)) }) } });
    bool found = false;
    for (const json& c : saved["actors"][0]["components"]) {
        if (c.value("type", std::string()) != "com.acme.weather/Rain") continue;
        found = true;
        EXPECT_DOUBLE_EQ(c["intensity"].get<double>(), 0.75);
        EXPECT_EQ(c["tags"], json::array({ "wet", "cold" }));
        EXPECT_EQ(c["nested"]["a"], 1);
    }
    EXPECT_TRUE(found) << saved.dump(2);
}

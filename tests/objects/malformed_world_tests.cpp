#include <gtest/gtest.h>

#include "plugins/plugin_test_utils.hpp"
#include "support/test_engine_context.hpp"

#include "engine/world/actor.hpp"
#include "engine/world/world.hpp"

using namespace Shard::Engine;
using PluginTest::TempDir;

// Hand-edited, tool-made or half-written .world files must not crash the loader. Wrong-shaped parts are reported and skipped. These are the fuzzer's crashing inputs (2026-10-22).
class MalformedWorldTest : public ::testing::Test {
protected:
    void SetUp() override { Core::SetEngine(&context); }
    void TearDown() override { Core::SetEngine(nullptr); }

    std::unique_ptr<Worlds::World> Load(const std::string& text)
    {
        dir.Write("in.world", text);
        auto world = std::make_unique<Worlds::World>("w", Filesystem::Path(dir.Sub("in.world")));
        EXPECT_NO_THROW(world->Deserialize(Filesystem::Path(dir.Sub("in.world")))) << text;
        return world;
    }

    Shard::Tests::TestEngineContext context;
    TempDir dir;
};

TEST_F(MalformedWorldTest, NonStringActorNameGetsDefault)
{
    auto world = Load(R"({ "name": "w", "actors": [ { "name": {}, "components": [] } ] })");
    auto roots = world->GetRootActors();
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots.begin()->second->GetName(), "Actor");
}

TEST_F(MalformedWorldTest, ActorWithoutNameLoads)
{
    auto world = Load(R"({ "name": "w", "actors": [ { "components": [] } ] })");
    EXPECT_EQ(world->GetRootActors().size(), 1u);
}

TEST_F(MalformedWorldTest, NonStringChildNameLoads)
{
    auto world = Load(R"({ "name": "w", "actors": [ { "name": "Parent", "components": [],
        "children": [ { "name": 3, "components": [] }, 7 ] } ] })");
    auto roots = world->GetRootActors();
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots.begin()->second->GetChildrenID(false).size(), 1u);
}

TEST_F(MalformedWorldTest, NonStringComponentTypeSkipped)
{
    auto world = Load(R"({ "name": "w", "actors": [ { "name": "A", "components": [
        { "type": ["a"] }, 12, { "type": "transform", "position": { "x": 1.0, "y": 2.0, "z": 3.0 } } ] } ] })");
    auto roots = world->GetRootActors();
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_TRUE(roots.begin()->second->GetUnknownComponents().empty());
}

TEST_F(MalformedWorldTest, BadComponentDroppedAlone)
{
    auto world = Load(R"({ "name": "w", "actors": [
        { "name": "Bad", "components": [ { "type": "transform", "position": "here" } ] },
        { "name": "Good", "components": [] } ] })");
    EXPECT_EQ(world->GetRootActors().size(), 2u);
}

TEST_F(MalformedWorldTest, NonObjectActorEntriesSkipped)
{
    auto world = Load(R"({ "name": "w", "actors": [ 1, "two", null, { "name": "Real", "components": [] } ] })");
    EXPECT_EQ(world->GetRootActors().size(), 1u);
}

TEST_F(MalformedWorldTest, NonArrayActorsLoadEmpty)
{
    auto world = Load(R"({ "name": "w", "actors": 5 })");
    EXPECT_TRUE(world->GetRootActors().empty());
}

TEST_F(MalformedWorldTest, NonStringWorldNameUsesFile)
{
    auto world = Load(R"({ "name": [], "actors": [] })");
    EXPECT_EQ(world->GetName(), "in");
}

TEST_F(MalformedWorldTest, NonObjectFileLoadsNothing)
{
    auto world = Load(R"([ 1, 2, 3 ])");
    EXPECT_TRUE(world->GetRootActors().empty());
    Load("{ not json");
}

TEST_F(MalformedWorldTest, CollectAssetsDoesNotThrow)
{
    dir.Write("in.world", R"({ "name": "w", "actors": [ 1, { "name": "A", "components": [ 2, { "type": {} } ], "children": [ "x" ] } ] })");
    EXPECT_NO_THROW(Worlds::CollectWorldAssetRefs(Filesystem::Path(dir.Sub("in.world"))));
}

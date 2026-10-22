#include <gtest/gtest.h>

#include "plugins/plugin_test_utils.hpp"
#include "support/test_engine_context.hpp"

#include "engine/world/actor.hpp"
#include "engine/world/world.hpp"

using namespace Shard::Engine;
using PluginTest::TempDir;

// A .world edited by hand (or by a tool, or half written) must not take the engine down : what has the wrong shape is reported and
// skipped, the rest loads. These are the files the fuzzer of 2026-10-22 crashed the loader with.
class MalformedWorldTest : public ::testing::Test {
protected:
    void SetUp() override { Core::SetEngine(&context); }
    void TearDown() override { Core::SetEngine(nullptr); }

    // Loads `text` as a world file : must not throw
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

TEST_F(MalformedWorldTest, AnActorWhoseNameIsNotAStringGetsADefaultOne)
{
    auto world = Load(R"({ "name": "w", "actors": [ { "name": {}, "components": [] } ] })");
    auto roots = world->GetRootActors();
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots.begin()->second->GetName(), "Actor");
}

TEST_F(MalformedWorldTest, AnActorWithoutANameStillLoads)
{
    auto world = Load(R"({ "name": "w", "actors": [ { "components": [] } ] })");
    EXPECT_EQ(world->GetRootActors().size(), 1u);
}

TEST_F(MalformedWorldTest, AChildWhoseNameIsNotAStringStillLoads)
{
    auto world = Load(R"({ "name": "w", "actors": [ { "name": "Parent", "components": [],
        "children": [ { "name": 3, "components": [] }, 7 ] } ] })");
    auto roots = world->GetRootActors();
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots.begin()->second->GetChildrenID(false).size(), 1u);     // the 7 is skipped
}

TEST_F(MalformedWorldTest, AComponentWhoseTypeIsNotAStringIsSkipped)
{
    auto world = Load(R"({ "name": "w", "actors": [ { "name": "A", "components": [
        { "type": ["a"] }, 12, { "type": "transform", "position": { "x": 1.0, "y": 2.0, "z": 3.0 } } ] } ] })");
    auto roots = world->GetRootActors();
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_TRUE(roots.begin()->second->GetUnknownComponents().empty());
}

TEST_F(MalformedWorldTest, AComponentWithBadDataIsDroppedAloneAndTheOtherActorsLoad)
{
    auto world = Load(R"({ "name": "w", "actors": [
        { "name": "Bad", "components": [ { "type": "transform", "position": "here" } ] },
        { "name": "Good", "components": [] } ] })");
    EXPECT_EQ(world->GetRootActors().size(), 2u);
}

TEST_F(MalformedWorldTest, EntriesOfActorsThatAreNotObjectsAreSkipped)
{
    auto world = Load(R"({ "name": "w", "actors": [ 1, "two", null, { "name": "Real", "components": [] } ] })");
    EXPECT_EQ(world->GetRootActors().size(), 1u);
}

TEST_F(MalformedWorldTest, ActorsThatAreNotAnArrayLoadAnEmptyWorld)
{
    auto world = Load(R"({ "name": "w", "actors": 5 })");
    EXPECT_TRUE(world->GetRootActors().empty());
}

TEST_F(MalformedWorldTest, AWorldWhoseNameIsNotAStringIsNamedAfterItsFile)
{
    auto world = Load(R"({ "name": [], "actors": [] })");
    EXPECT_EQ(world->GetName(), "in");
}

TEST_F(MalformedWorldTest, AFileThatIsNotAnObjectLoadsNothing)
{
    auto world = Load(R"([ 1, 2, 3 ])");
    EXPECT_TRUE(world->GetRootActors().empty());
    Load("{ not json");
}

TEST_F(MalformedWorldTest, CollectingTheAssetsOfAMalformedWorldDoesNotThrow)
{
    dir.Write("in.world", R"({ "name": "w", "actors": [ 1, { "name": "A", "components": [ 2, { "type": {} } ], "children": [ "x" ] } ] })");
    EXPECT_NO_THROW(Worlds::CollectWorldAssetRefs(Filesystem::Path(dir.Sub("in.world"))));
}

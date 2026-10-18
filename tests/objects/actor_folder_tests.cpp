#include <gtest/gtest.h>

#include "support/test_engine_context.hpp"

#include "engine/world/actor.hpp"
#include "engine/world/world.hpp"

using namespace Shard::Engine::Core;
using namespace Shard::Engine::Objects;

// The outliner folder of an actor is only an organization of the editor, saved with the actor
class ActorFolderTest : public ::testing::Test {
    protected:
        void SetUp() override {
            SetEngine(&context);
        }

        void TearDown() override {
            SetEngine(nullptr);
        }

        Shard::Tests::TestEngineContext context;
};

TEST_F(ActorFolderTest, ActorsStartAtTheTopLevel) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Lamp"), &context);

    EXPECT_TRUE(actor->GetFolder().empty());
}

TEST_F(ActorFolderTest, FolderRoundtripsThroughTheActor) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Lamp"), &context);

    actor->SetFolder("Lights/Indoor");

    EXPECT_EQ(actor->GetFolder(), "Lights/Indoor");
}

TEST_F(ActorFolderTest, FolderIsSerializedWhenSet) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Lamp"), &context);
    actor->SetFolder("Lights/Indoor");

    const ordered_json data = Shard::Engine::Worlds::SerializeActorToJson(actor);

    ASSERT_TRUE(data.contains("folder"));
    EXPECT_EQ(data["folder"].get<std::string>(), "Lights/Indoor");
}

TEST_F(ActorFolderTest, NoFolderLeavesTheFileUnchanged) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Lamp"), &context);

    const ordered_json data = Shard::Engine::Worlds::SerializeActorToJson(actor);

    EXPECT_FALSE(data.contains("folder"));
}

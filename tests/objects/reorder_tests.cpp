#include <gtest/gtest.h>

#include "support/test_engine_context.hpp"

#include "engine/world/actor.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_object.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Core;
using namespace Shard::Engine::Objects;

namespace {

    std::vector<ObjectID> Ids(std::initializer_list<int> values) {
        std::vector<ObjectID> ids;
        for (int v : values)
            ids.emplace_back(v);
        return ids;
    }

    std::vector<int> AsInts(const std::vector<ObjectID>& ids) {
        std::vector<int> out;
        for (const ObjectID& id : ids)
            out.push_back(id.GetAsInt());
        return out;
    }

}

TEST(IdOrderTest, MoveNextToPutsItemBeforeOrAfterAnchor) {
    auto list = Ids({ 1, 2, 3, 4 });

    EXPECT_TRUE(MoveIdNextTo(list, ObjectID(4), ObjectID(1), false));
    EXPECT_EQ(AsInts(list), (std::vector<int>{ 4, 1, 2, 3 }));

    EXPECT_TRUE(MoveIdNextTo(list, ObjectID(4), ObjectID(2), true));
    EXPECT_EQ(AsInts(list), (std::vector<int>{ 1, 2, 4, 3 }));

    EXPECT_TRUE(MoveIdNextTo(list, ObjectID(1), ObjectID(3), true));
    EXPECT_EQ(AsInts(list), (std::vector<int>{ 2, 4, 3, 1 }));
}

TEST(IdOrderTest, MoveNextToRejectsUnknownOrSameIds) {
    auto list = Ids({ 1, 2, 3 });

    EXPECT_FALSE(MoveIdNextTo(list, ObjectID(9), ObjectID(1), true));
    EXPECT_FALSE(MoveIdNextTo(list, ObjectID(1), ObjectID(9), true));
    EXPECT_FALSE(MoveIdNextTo(list, ObjectID(2), ObjectID(2), true));
    EXPECT_EQ(AsInts(list), (std::vector<int>{ 1, 2, 3 }));
}

TEST(IdOrderTest, ApplyOrderPutsListedFirstAndKeepsTheRest) {
    auto list = Ids({ 1, 2, 3, 4, 5 });

    ApplyIdOrder(list, Ids({ 4, 9, 2 }));

    // 9 is not in the list and is ignored, the unlisted ones keep their relative order after the listed ones
    EXPECT_EQ(AsInts(list), (std::vector<int>{ 4, 2, 1, 3, 5 }));
}

class ReorderTest : public ::testing::Test {
    protected:
        void SetUp() override {
            SetEngine(&context);
            world = std::make_unique<Worlds::World>("w", Filesystem::Path("w.world"));
        }

        void TearDown() override {
            world.reset();
            SetEngine(nullptr);
        }

        std::shared_ptr<Actor> Spawn(const std::string& name, std::shared_ptr<Actor> parent = nullptr) {
            auto actor = Object::CreateWithContext<Actor>(&context, name, &context);
            if (parent)
                parent->AddChild(actor);
            else
                world->AddActor(actor);
            return actor;
        }

        static std::vector<std::string> Names(const std::vector<std::shared_ptr<Actor>>& actors) {
            std::vector<std::string> names;
            for (const auto& a : actors)
                names.push_back(a->GetName());
            return names;
        }

        Shard::Tests::TestEngineContext context;
        std::unique_ptr<Worlds::World> world;
};

TEST_F(ReorderTest, RootActorsKeepCreationOrderByDefault) {
    Spawn("A");
    Spawn("B");
    Spawn("C");

    EXPECT_EQ(Names(world->GetRootActorsOrdered()), (std::vector<std::string>{ "A", "B", "C" }));
}

TEST_F(ReorderTest, RootActorMovesNextToAnotherOne) {
    auto a = Spawn("A");
    auto b = Spawn("B");
    auto c = Spawn("C");

    EXPECT_TRUE(c->MoveNextTo(a, false));
    EXPECT_EQ(Names(world->GetRootActorsOrdered()), (std::vector<std::string>{ "C", "A", "B" }));

    EXPECT_TRUE(a->MoveNextTo(b, true));
    EXPECT_EQ(Names(world->GetRootActorsOrdered()), (std::vector<std::string>{ "C", "B", "A" }));
}

TEST_F(ReorderTest, RemovedActorLeavesTheOrder) {
    auto a = Spawn("A");
    auto b = Spawn("B");
    auto c = Spawn("C");

    world->RemoveActor(b->GetID());

    EXPECT_EQ(Names(world->GetRootActorsOrdered()), (std::vector<std::string>{ "A", "C" }));
}

TEST_F(ReorderTest, ChildrenReorderUnderTheirParentOnly) {
    auto parent = Spawn("Parent");
    auto x = Spawn("X", parent);
    auto y = Spawn("Y", parent);
    auto z = Spawn("Z", parent);
    auto root = Spawn("Root");

    EXPECT_TRUE(z->MoveNextTo(x, false));
    EXPECT_EQ(AsInts(parent->GetChildrenID()), (std::vector<int>{ z->GetID().GetAsInt(), x->GetID().GetAsInt(), y->GetID().GetAsInt() }));

    // Different parents : refused, nothing changes
    EXPECT_FALSE(y->MoveNextTo(root, true));
    EXPECT_FALSE(root->MoveNextTo(y, true));
}

TEST_F(ReorderTest, ComponentsMoveButTheTransformStaysFirst) {
    auto actor = Spawn("Actor");
    auto s1 = actor->AddComponent<Script>();
    auto s2 = actor->AddComponent<Script>();
    auto s3 = actor->AddComponent<Script>();
    ASSERT_TRUE(s1 && s2 && s3);

    const auto& list = actor->GetComponents();
    ASSERT_EQ(list.size(), 4u);
    EXPECT_EQ(list[0], actor->transform);

    EXPECT_TRUE(actor->MoveComponent(static_cast<size_t>(3), static_cast<size_t>(1)));
    EXPECT_EQ(list[1], s3);
    EXPECT_EQ(list[2], s1);
    EXPECT_EQ(list[3], s2);

    // The Transform can neither move nor be displaced
    EXPECT_FALSE(actor->MoveComponent(static_cast<size_t>(0), static_cast<size_t>(2)));
    EXPECT_FALSE(actor->MoveComponent(static_cast<size_t>(2), static_cast<size_t>(0)));
    EXPECT_FALSE(actor->MoveComponent(static_cast<size_t>(2), static_cast<size_t>(2)));
    EXPECT_FALSE(actor->MoveComponent(static_cast<size_t>(2), static_cast<size_t>(9)));
    EXPECT_EQ(list[0], actor->transform);

    actor->SetComponentsOrder({ s2, s1, s3 });
    EXPECT_EQ(list[0], actor->transform);
    EXPECT_EQ(list[1], s2);
    EXPECT_EQ(list[2], s1);
    EXPECT_EQ(list[3], s3);
}

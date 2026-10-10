#include <gtest/gtest.h>

#include "support/test_engine_context.hpp"

#include "engine/core/ecs/registry.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/ecs_components.hpp"
#include "engine/renderer/components/camera.hpp"
#include "engine/physics/physics_body.hpp"

using namespace Shard::Engine::Core;
using namespace Shard::Engine::Objects;
using namespace Shard::Engine::Objects::Components;

class ActorTest : public ::testing::Test {
    protected:
        void SetUp() override {
            SetEngine(&context);
        }

        void TearDown() override {
            SetEngine(nullptr);
        }

        Shard::Tests::TestEngineContext context;
};

TEST_F(ActorTest, CreationAutoAddsTransformComponent) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    ASSERT_NE(actor->transform, nullptr);
    EXPECT_TRUE(actor->HasComponent<Transform>());
    EXPECT_EQ(actor->GetComponents().size(), 1u);
}

TEST_F(ActorTest, GetNameAndSetNameRoundtrip) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    EXPECT_EQ(actor->GetName(), "Player");

    actor->SetName("Renamed");

    EXPECT_EQ(actor->GetName(), "Renamed");
}

TEST_F(ActorTest, AddComponentAppendsToComponentList) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    auto camera = actor->AddComponent<Camera>();

    ASSERT_NE(camera, nullptr);
    EXPECT_TRUE(actor->HasComponent<Camera>());
    EXPECT_EQ(actor->GetComponents().size(), 2u);
}

TEST_F(ActorTest, HasComponentIsFalseForComponentTypeNeverAdded) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    EXPECT_FALSE(actor->HasComponent<PhysicsBody>());
}

TEST_F(ActorTest, GetComponentReturnsNthMatchByInsertionOrder) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    auto first = actor->AddComponent<Camera>();
    auto second = actor->AddComponent<Camera>();

    EXPECT_EQ(actor->GetComponent<Camera>(0), first);
    EXPECT_EQ(actor->GetComponent<Camera>(1), second);
}

TEST_F(ActorTest, GetComponentsReturnsAllMatchesOfType) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    actor->AddComponent<Camera>();
    actor->AddComponent<Camera>();

    EXPECT_EQ(actor->GetComponents<Camera>().size(), 2u);
}

TEST_F(ActorTest, AddingSecondTransformFailsAndReturnsNull) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    auto second = actor->AddComponent<Transform>();

    EXPECT_EQ(second, nullptr);
    EXPECT_EQ(actor->GetComponents().size(), 1u);
}

TEST_F(ActorTest, IsActiveDefaultsToTrue) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    EXPECT_TRUE(actor->IsActive());
}

TEST_F(ActorTest, ComponentParentPointsBackToOwningActor) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    auto camera = actor->AddComponent<Camera>();

    EXPECT_EQ(camera->parent, actor);
}

TEST_F(ActorTest, GetComponentIDInWorldPacksActorAndComponentIndex) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    int packed = actor->GetComponentIDInWorld(2);

    EXPECT_EQ(packed, (actor->GetID().GetAsInt() << 12) | 2);
}

TEST_F(ActorTest, ActorAndItsComponentsResolveTheInjectedEngineContext) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);
    auto camera = actor->AddComponent<Camera>();

    EXPECT_EQ(actor->GetEngineContext(), &context);
    EXPECT_EQ(camera->GetEngineContext(), &context);
}

// The Actor / Component API is a facade : every actor owns an entity of the engine's registry, kept in sync with it

TEST_F(ActorTest, EveryActorOwnsAnEntityLinkedBackToIt) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    Ecs::Registry* ecs = context.GetEcs();
    ASSERT_TRUE(ecs->IsAlive(actor->GetEntity()));
    const auto* link = ecs->TryGet<Shard::Engine::Worlds::ActorLink>(actor->GetEntity());
    ASSERT_NE(link, nullptr);
    EXPECT_EQ(link->objectId, actor->GetID().GetAsInt());
}

TEST_F(ActorTest, ActorHierarchyIsMirroredByTheEntities) {
    auto parent = Object::CreateWithContext<Actor>(&context, std::string("Parent"), &context);
    auto a = Object::CreateWithContext<Actor>(&context, std::string("A"), &context);
    auto b = Object::CreateWithContext<Actor>(&context, std::string("B"), &context);
    parent->AddChild(a);
    parent->AddChild(b);

    Ecs::Registry* ecs = context.GetEcs();
    EXPECT_EQ(ecs->GetParent(a->GetEntity()), parent->GetEntity());
    EXPECT_EQ(ecs->ChildCount(parent->GetEntity()), 2u);
    EXPECT_EQ(ecs->FirstChild(parent->GetEntity()), a->GetEntity());

    b->SetParent(a);                                           // reparent through the actor API
    EXPECT_EQ(ecs->GetParent(b->GetEntity()), a->GetEntity());
    EXPECT_EQ(ecs->ChildCount(parent->GetEntity()), 1u);

    b->SetParent(nullptr);                                     // back to the root
    EXPECT_EQ(ecs->GetParent(b->GetEntity()), Ecs::kNullEntity);
}

TEST_F(ActorTest, DestroyingAnActorReleasesItsEntityAndItsChildrens) {
    auto parent = Object::CreateWithContext<Actor>(&context, std::string("Parent"), &context);
    auto child = Object::CreateWithContext<Actor>(&context, std::string("Child"), &context);
    parent->AddChild(child);

    Ecs::Registry* ecs = context.GetEcs();
    const Ecs::Entity parentEntity = parent->GetEntity();
    const Ecs::Entity childEntity = child->GetEntity();
    const size_t before = ecs->Count();

    parent->Destroy();

    EXPECT_FALSE(ecs->IsAlive(parentEntity));
    EXPECT_FALSE(ecs->IsAlive(childEntity));
    EXPECT_EQ(ecs->Count(), before - 2);
}

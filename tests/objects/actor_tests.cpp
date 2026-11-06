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

TEST_F(ActorTest, NameRoundtrip) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    EXPECT_EQ(actor->GetName(), "Player");

    actor->SetName("Renamed");

    EXPECT_EQ(actor->GetName(), "Renamed");
}

TEST_F(ActorTest, AddComponentAppends) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    auto camera = actor->AddComponent<Camera>();

    ASSERT_NE(camera, nullptr);
    EXPECT_TRUE(actor->HasComponent<Camera>());
    EXPECT_EQ(actor->GetComponents().size(), 2u);
}

TEST_F(ActorTest, HasComponentFalseWhenAbsent) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    EXPECT_FALSE(actor->HasComponent<PhysicsBody>());
}

TEST_F(ActorTest, GetComponentNthMatch) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    auto first = actor->AddComponent<Camera>();
    auto second = actor->AddComponent<Camera>();

    EXPECT_EQ(actor->GetComponent<Camera>(0), first);
    EXPECT_EQ(actor->GetComponent<Camera>(1), second);
}

TEST_F(ActorTest, GetComponentsAllMatches) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    actor->AddComponent<Camera>();
    actor->AddComponent<Camera>();

    EXPECT_EQ(actor->GetComponents<Camera>().size(), 2u);
}

TEST_F(ActorTest, SecondTransformFails) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    auto second = actor->AddComponent<Transform>();

    EXPECT_EQ(second, nullptr);
    EXPECT_EQ(actor->GetComponents().size(), 1u);
}

TEST_F(ActorTest, IsActiveDefaultsToTrue) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    EXPECT_TRUE(actor->IsActive());
}

TEST_F(ActorTest, ComponentParentIsOwner) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    auto camera = actor->AddComponent<Camera>();

    EXPECT_EQ(camera->parent, actor);
}

TEST_F(ActorTest, ComponentIdPacksIndices) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    int packed = actor->GetComponentIDInWorld(2);

    EXPECT_EQ(packed, (actor->GetID().GetAsInt() << 12) | 2);
}

TEST_F(ActorTest, ComponentsResolveEngineContext) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);
    auto camera = actor->AddComponent<Camera>();

    EXPECT_EQ(actor->GetEngineContext(), &context);
    EXPECT_EQ(camera->GetEngineContext(), &context);
}

// The Actor/Component API is a facade: every actor owns an entity in the engine registry, kept in sync.

TEST_F(ActorTest, ActorOwnsLinkedEntity) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Player"), &context);

    Ecs::Registry* ecs = context.GetEcs();
    ASSERT_TRUE(ecs->IsAlive(actor->GetEntity()));
    const auto* link = ecs->TryGet<Shard::Engine::Worlds::ActorLink>(actor->GetEntity());
    ASSERT_NE(link, nullptr);
    EXPECT_EQ(link->objectId, actor->GetID().GetAsInt());
}

TEST_F(ActorTest, HierarchyMirroredInEntities) {
    auto parent = Object::CreateWithContext<Actor>(&context, std::string("Parent"), &context);
    auto a = Object::CreateWithContext<Actor>(&context, std::string("A"), &context);
    auto b = Object::CreateWithContext<Actor>(&context, std::string("B"), &context);
    parent->AddChild(a);
    parent->AddChild(b);

    Ecs::Registry* ecs = context.GetEcs();
    EXPECT_EQ(ecs->GetParent(a->GetEntity()), parent->GetEntity());
    EXPECT_EQ(ecs->ChildCount(parent->GetEntity()), 2u);
    EXPECT_EQ(ecs->FirstChild(parent->GetEntity()), a->GetEntity());

    b->SetParent(a);
    EXPECT_EQ(ecs->GetParent(b->GetEntity()), a->GetEntity());
    EXPECT_EQ(ecs->ChildCount(parent->GetEntity()), 1u);

    b->SetParent(nullptr);
    EXPECT_EQ(ecs->GetParent(b->GetEntity()), Ecs::kNullEntity);
}

TEST_F(ActorTest, DestroyReleasesEntities) {
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

// Each child removes itself from its parent's list while destroyed. Walking that list directly skipped every other one.
TEST_F(ActorTest, DestroyDestroysChildren) {
    auto parent = Object::CreateWithContext<Actor>(&context, std::string("Parent"), &context);
    std::vector<std::shared_ptr<Actor>> children;
    for (int i = 0; i < 4; i++) {
        children.push_back(Object::CreateWithContext<Actor>(&context, "Child " + std::to_string(i), &context));
        parent->AddChild(children.back());
    }

    Ecs::Registry* ecs = context.GetEcs();
    std::vector<Ecs::Entity> entities;
    for (auto& child : children)
        entities.push_back(child->GetEntity());

    parent->Destroy();

    for (size_t i = 0; i < children.size(); i++) {
        EXPECT_FALSE(ecs->IsAlive(entities[i])) << "child " << i;
        EXPECT_EQ(context.GetObjectIDManager()->GetObjectFromID(children[i]->GetID()), nullptr) << "child " << i;
    }
}

TEST_F(ActorTest, TransformMirroredOnEntity) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Mover"), &context);
    Ecs::Registry* ecs = context.GetEcs();

    const auto* mirror = ecs->TryGet<Shard::Engine::Worlds::LocalTransform>(actor->GetEntity());
    ASSERT_NE(mirror, nullptr);
    EXPECT_EQ(mirror->scale, glm::vec3(1.0f));

    actor->transform->SetPosition(glm::vec3(1, 2, 3));
    actor->transform->SetScale(glm::vec3(2, 2, 2));
    mirror = ecs->TryGet<Shard::Engine::Worlds::LocalTransform>(actor->GetEntity());
    EXPECT_EQ(mirror->position, glm::vec3(1, 2, 3));
    EXPECT_EQ(mirror->scale, glm::vec3(2, 2, 2));

    int seen = 0;
    ecs->Each<const Shard::Engine::Worlds::LocalTransform>([&](const Shard::Engine::Worlds::LocalTransform& t) {
        if (t.position == glm::vec3(1, 2, 3)) ++seen;
    });
    EXPECT_EQ(seen, 1);
}

TEST_F(ActorTest, DestroyInQueryIsDeferred) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Doomed"), &context);
    Ecs::Registry* ecs = context.GetEcs();
    const Ecs::Entity entity = actor->GetEntity();

    ecs->Each<Shard::Engine::Worlds::ActorLink>([&](Shard::Engine::Worlds::ActorLink&) { actor->Destroy(); });
    EXPECT_TRUE(ecs->IsAlive(entity));
    ecs->FlushDeferred();
    EXPECT_FALSE(ecs->IsAlive(entity));
}

// Actor and components point at each other. After destroy plus world change (object IDs reset) nothing may survive, or a model's mesh stays loaded and its asset can never be deleted.
TEST_F(ActorTest, DestroyedActorFreedOnChange) {
    std::weak_ptr<Actor> weakActor;
    std::weak_ptr<Transform> weakTransform;
    {
        auto actor = Object::CreateWithContext<Actor>(&context, std::string("Doomed"), &context);
        weakActor = actor;
        weakTransform = actor->transform;
        actor->Destroy();
    }
    EXPECT_TRUE(weakActor.expired());

    context.GetObjectIDManager()->Reset();
    EXPECT_TRUE(weakTransform.expired());
}


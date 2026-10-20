#include <gtest/gtest.h>

#include "support/test_engine_context.hpp"

#include "engine/assets/serialization/reflection/reflection_serializer.hpp"
#include "engine/core/tags/tag_registry.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/event_system.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_tags.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Core;
using namespace Shard::Engine::Objects;

// Tags on worlds, actors and components : what is saved, the world's index, the events
class TagWorldTest : public ::testing::Test {
    protected:
        void SetUp() override {
            SetEngine(&context);
            TagRegistry::Get().Clear();
            TagRegistry::Get().SetDeclarations(TagSource::Project, "", {
                { "Enemy.Undead.Skeleton", "" }, { "Enemy.Boss", "" }, { "Player", "" }, { "Status.Dead", "" }, { "Hitbox.Head", "" },
            });
            world = std::make_unique<Worlds::World>("w", Filesystem::Path("w.world"));
        }

        void TearDown() override {
            world.reset();
            TagRegistry::Get().Clear();
            SetEngine(nullptr);
        }

        std::shared_ptr<Actor> Spawn(const std::string& name, std::initializer_list<const char*> tags = {}, std::shared_ptr<Actor> parent = nullptr) {
            auto actor = Object::CreateWithContext<Actor>(&context, name, &context);
            for (const char* tag : tags)
                actor->GetTags().Add(Tag::Request(tag));
            if (parent)
                parent->AddChild(actor);
            else
                world->AddActor(actor);
            return actor;
        }

        static std::vector<std::string> Names(const std::vector<std::shared_ptr<Actor>>& actors) {
            std::vector<std::string> names;
            for (auto& a : actors)
                names.push_back(a->GetName());
            return names;
        }

        static Tag T(const char* name) { return Tag::Request(name); }

        Shard::Tests::TestEngineContext context;
        std::unique_ptr<Worlds::World> world;
};

// ---- The index ----

TEST_F(TagWorldTest, FindsTheActorsUnderATag) {
    Spawn("Skeleton", { "Enemy.Undead.Skeleton" });
    Spawn("Boss", { "Enemy.Boss" });
    Spawn("Hero", { "Player" });

    EXPECT_EQ(Names(world->FindActorsWithTag(T("Enemy"))), (std::vector<std::string>{ "Skeleton", "Boss" }));
    EXPECT_EQ(Names(world->FindActorsWithTag(T("Enemy.Undead"))), (std::vector<std::string>{ "Skeleton" }));
    EXPECT_TRUE(world->FindActorsWithTag(T("Status")).empty());
    EXPECT_EQ(world->FindFirstActorWithTag(T("Player"))->GetName(), "Hero");
    EXPECT_EQ(world->FindFirstActorWithTag(T("Status")), nullptr);
}

TEST_F(TagWorldTest, ResultsAreInTheOrderOfTheIds) {
    Spawn("A", { "Enemy.Boss" });
    Spawn("B", { "Enemy.Undead.Skeleton" });
    Spawn("C", { "Enemy.Boss" });

    const auto found = world->FindActorsWithTag(T("Enemy"));
    ASSERT_EQ(found.size(), 3u);
    for (size_t i = 1; i < found.size(); ++i)
        EXPECT_LT(found[i - 1]->GetID(), found[i]->GetID());
}

TEST_F(TagWorldTest, ChildActorsAreIndexedButDoNotInherit) {
    auto boss = Spawn("Boss", { "Enemy.Boss" });
    Spawn("Head", { "Hitbox.Head" }, boss);

    EXPECT_EQ(Names(world->FindActorsWithTag(T("Hitbox"))), (std::vector<std::string>{ "Head" }));
    EXPECT_EQ(Names(world->FindActorsWithTag(T("Enemy"))), (std::vector<std::string>{ "Boss" }));
}

TEST_F(TagWorldTest, TheIndexFollowsTheChanges) {
    auto actor = Spawn("Grunt");
    EXPECT_TRUE(world->FindActorsWithTag(T("Enemy")).empty());

    actor->GetTags().Add(T("Enemy.Boss"));
    EXPECT_EQ(world->FindActorsWithTag(T("Enemy")).size(), 1u);

    actor->GetTags().Remove(T("Enemy.Boss"));
    EXPECT_TRUE(world->FindActorsWithTag(T("Enemy")).empty());

    actor->GetTags() = TagContainer{ T("Player") };
    EXPECT_EQ(world->FindActorsWithTag(T("Player")).size(), 1u);
}

TEST_F(TagWorldTest, ADestroyedActorLeavesTheIndex) {
    auto actor = Spawn("Skeleton", { "Enemy.Undead.Skeleton" });
    actor->Destroy();
    EXPECT_TRUE(world->FindActorsWithTag(T("Enemy")).empty());
}

TEST_F(TagWorldTest, DeactivatedActorsAreLeftOutUnlessAsked) {
    auto actor = Spawn("Skeleton", { "Enemy.Undead.Skeleton" });
    actor->DeActivate();

    EXPECT_TRUE(world->FindActorsWithTag(T("Enemy")).empty());
    EXPECT_EQ(world->FindActorsWithTag(T("Enemy"), true).size(), 1u);
}

TEST_F(TagWorldTest, FindsActorsByQuery) {
    Spawn("Skeleton", { "Enemy.Undead.Skeleton" });
    Spawn("Corpse", { "Enemy.Undead.Skeleton", "Status.Dead" });
    Spawn("Boss", { "Enemy.Boss" });
    Spawn("Hero", { "Player" });
    Spawn("Rock");

    EXPECT_EQ(Names(world->FindActors(TagQuery::Parse("Enemy & !(Enemy.Boss | Status.Dead)"))), (std::vector<std::string>{ "Skeleton" }));
    // No required tag : every actor is tested
    EXPECT_EQ(Names(world->FindActors(TagQuery::Parse("Player | Enemy.Boss"))), (std::vector<std::string>{ "Boss", "Hero" }));
    EXPECT_EQ(Names(world->FindActors(TagQuery::Parse("!Enemy & !Player"))), (std::vector<std::string>{ "Rock" }));
    EXPECT_EQ(world->FindActors(TagQuery()).size(), 5u);
}

TEST_F(TagWorldTest, SpatialQueriesFilterByTag) {
    auto near = Spawn("Near", { "Enemy.Boss" });
    auto far = Spawn("Far", { "Enemy.Boss" });
    auto friendly = Spawn("Friendly", { "Player" });
    near->transform->SetPosition(glm::vec3(3, 0, 0));
    far->transform->SetPosition(glm::vec3(30, 0, 0));
    friendly->transform->SetPosition(glm::vec3(1, 0, 0));

    std::vector<std::shared_ptr<Actor>> found;
    world->QuerySphere(DVec3(0.0), 10.0, TagQuery::Parse("Enemy"), found);
    EXPECT_EQ(Names(found), (std::vector<std::string>{ "Near" }));

    world->QueryAABB(AABB(DVec3(-1.0), DVec3(50.0)), TagQuery::Parse("Enemy"), found);
    EXPECT_EQ(Names(found), (std::vector<std::string>{ "Near", "Far" }));
}

TEST_F(TagWorldTest, FindsComponentsByTag) {
    auto boss = Spawn("Boss");
    boss->transform->GetTags().Add(T("Hitbox.Head"));

    auto found = world->FindComponentsWithTag<Components::Transform>(T("Hitbox"));
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0], boss->transform);

    boss->transform->GetTags().Clear();
    EXPECT_TRUE(world->FindComponentsWithTag(T("Hitbox")).empty());
}

TEST_F(TagWorldTest, ComponentTagsAreIndexedWhenTheActorJoins) {
    auto actor = Object::CreateWithContext<Actor>(&context, std::string("Late"), &context);
    actor->transform->GetTags().Add(T("Hitbox.Head"));
    actor->GetTags().Add(T("Player"));
    world->AddActor(actor);

    EXPECT_EQ(world->FindActorsWithTag(T("Player")).size(), 1u);
}

TEST_F(TagWorldTest, AClonedActorHasTheSameTags) {
    auto original = Spawn("Skeleton", { "Enemy.Undead.Skeleton" });
    original->transform->GetTags().Add(T("Hitbox.Head"));

    auto copy = original->Clone();
    EXPECT_EQ(copy->GetTags(), original->GetTags());
    EXPECT_EQ(copy->transform->GetTags(), original->transform->GetTags());
    EXPECT_EQ(world->FindActorsWithTag(T("Enemy")).size(), 2u);

    // Each its own : changing the copy does not change the original
    copy->GetTags().Clear();
    EXPECT_FALSE(original->GetTags().IsEmpty());
    EXPECT_EQ(world->FindActorsWithTag(T("Enemy")).size(), 1u);
}

// ---- Files ----

TEST_F(TagWorldTest, ActorAndComponentTagsAreSaved) {
    auto actor = Spawn("Skeleton", { "Status.Dead", "Enemy.Undead.Skeleton" });
    actor->transform->GetTags().Add(T("Hitbox.Head"));

    const ordered_json data = Worlds::SerializeActorToJson(actor);
    ASSERT_TRUE(data.contains("tags"));
    EXPECT_EQ(data["tags"], ordered_json::array({ "Enemy.Undead.Skeleton", "Status.Dead" }));
    EXPECT_EQ(data["components"][0]["tags"], ordered_json::array({ "Hitbox.Head" }));
}

TEST_F(TagWorldTest, NoTagsLeaveTheFileUnchanged) {
    auto actor = Spawn("Rock");
    const ordered_json data = Worlds::SerializeActorToJson(actor);
    EXPECT_FALSE(data.contains("tags"));
    EXPECT_FALSE(data["components"][0].contains("tags"));
}

TEST_F(TagWorldTest, TagsComeBackFromTheFile) {
    auto actor = Spawn("Skeleton", { "Enemy.Undead.Skeleton" });
    actor->transform->GetTags().Add(T("Hitbox.Head"));
    const ordered_json data = Worlds::SerializeActorToJson(actor);
    actor->Destroy();

    auto loaded = Worlds::InstantiateActor(*world, data);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->GetTags(), (TagContainer{ T("Enemy.Undead.Skeleton") }));
    EXPECT_EQ(loaded->transform->GetTags(), (TagContainer{ T("Hitbox.Head") }));
    EXPECT_EQ(world->FindFirstActorWithTag(T("Enemy")), loaded);
    EXPECT_EQ(world->FindComponentsWithTag(T("Hitbox")).size(), 1u);
}

TEST_F(TagWorldTest, RenamedAndUnknownTagsAreReadBack) {
    TagRegistry::Get().SetRedirects({ { "Enemy.Undead.Skelly", "Enemy.Undead.Skeleton" } });
    const json data = json::parse(R"({ "name": "Old", "tags": ["Enemy.Undead.Skelly", "Weather.Rain"], "components": [] })");

    auto loaded = Worlds::InstantiateActor(*world, data);
    ASSERT_NE(loaded, nullptr);
    EXPECT_TRUE(loaded->GetTags().HasTagExact(T("Enemy.Undead.Skeleton")));

    // The plugin that declares it is not loaded : the tag is written back as it was read
    const ordered_json saved = Worlds::SerializeActorToJson(loaded);
    EXPECT_EQ(saved["tags"], ordered_json::array({ "Enemy.Undead.Skeleton", "Weather.Rain" }));
}

// ---- Events ----

TEST_F(TagWorldTest, ChangingTagsInALoadedWorldSendsAnEvent) {
    auto actor = Spawn("Grunt");
    world->SetLoaded(true);

    int calls = 0;
    TagContainer added;
    context.GetEventDispatcher()->subscribeToActor<Events::TagsChangedEvent>(actor->GetID(), [&](const Events::TagsChangedEvent& e) {
        ++calls;
        added = e.added;
    });

    actor->GetTags().Add(T("Status.Dead"));
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(added, (TagContainer{ T("Status.Dead") }));

    // Loading is quiet
    actor->GetTags().Assign(TagContainer{ T("Player") }, true);
    EXPECT_EQ(calls, 1);
    world->SetLoaded(false);
}

TEST_F(TagWorldTest, ComponentEventsGoToTheComponent) {
    auto actor = Spawn("Grunt");
    world->SetLoaded(true);

    int calls = 0;
    const auto componentID = Events::EventDispatcher::ComponentID(actor->GetComponentIDInWorld(actor->transform->GetLocalId()));
    context.GetEventDispatcher()->subscribeToComponent<Events::TagsChangedEvent>(componentID, [&](const Events::TagsChangedEvent&) { ++calls; });

    actor->transform->GetTags().Add(T("Hitbox.Head"));
    EXPECT_EQ(calls, 1);
    world->SetLoaded(false);
}

// ---- Reflected fields ----

namespace {
    struct Tagged {
        Tag damageType;
        TagContainer immune;
        TagQuery affects;
    };

    template<typename T>
    FieldInfo MakeField(const char* name, TypeID type, uint32_t offset)
    {
        return FieldInfo{ name, type, offset, Editable, 0, 0, nullptr, nullptr, nullptr, &CopyConstruct<T>, &Assign<T>, &Destroy<T>, &Equals<T> };
    }
}

TEST_F(TagWorldTest, ReflectedTagFieldsAreSavedByName) {
    FieldInfo damage = MakeField<Tag>("damageType", TypeID::Tag, offsetof(Tagged, damageType));
    FieldInfo immune = MakeField<TagContainer>("immune", TypeID::TagContainer, offsetof(Tagged, immune));
    FieldInfo affects = MakeField<TagQuery>("affects", TypeID::TagQuery, offsetof(Tagged, affects));
    const std::vector<FieldInfo*> fields = { &damage, &immune, &affects };
    Serialization::ReflectionContext reflection;

    Tagged in;
    in.damageType = T("Status.Dead");
    in.immune = TagContainer{ T("Player"), T("Enemy.Boss") };
    in.affects = TagQuery::Parse("Enemy & !Enemy.Boss");

    ordered_json out;
    Serialization::WriteFields(fields, &in, out, reflection);
    EXPECT_EQ(out["damageType"], "Status.Dead");
    EXPECT_EQ(out["immune"], ordered_json::array({ "Enemy.Boss", "Player" }));
    EXPECT_EQ(out["affects"], "Enemy & !Enemy.Boss");

    Tagged back;
    Serialization::ReadFields(fields, &back, json::parse(out.dump()), reflection, nullptr);
    EXPECT_EQ(back.damageType, in.damageType);
    EXPECT_EQ(back.immune, in.immune);
    EXPECT_EQ(back.affects, in.affects);
}

TEST_F(TagWorldTest, AnInvalidQueryInAFileKeepsTheDefault) {
    FieldInfo affects = MakeField<TagQuery>("affects", TypeID::TagQuery, offsetof(Tagged, affects));
    const std::vector<FieldInfo*> fields = { &affects };

    Tagged value;
    value.affects = TagQuery::Parse("Player");
    Serialization::ReadFields(fields, &value, json::parse(R"({ "affects": "Enemy &" })"), Serialization::ReflectionContext{}, nullptr);
    EXPECT_EQ(value.affects.ToString(), "Player");
}

// ---- Renames and references ----

TEST_F(TagWorldTest, RemapRenamesEveryUseInTheWorld) {
    world->GetTags().Add(T("Enemy.Boss"));
    auto actor = Spawn("Skeleton", { "Enemy.Undead.Skeleton", "Player" });
    actor->transform->GetTags().Add(T("Enemy.Boss"));

    TagRegistry::Get().SetDeclarations(TagSource::Project, "", { { "Foe.Undead.Skeleton", "" }, { "Foe.Boss", "" }, { "Player", "" } });
    const size_t changed = Worlds::RemapTags(*world, [](Tag tag) {
        const std::string name = tag.ToString();
        return name.rfind("Enemy", 0) == 0 ? TagRegistry::Get().Resolve("Foe" + name.substr(5)) : Tag();
    });

    EXPECT_EQ(changed, 3u);
    EXPECT_EQ(world->GetTags(), (TagContainer{ T("Foe.Boss") }));
    EXPECT_EQ(actor->GetTags(), (TagContainer{ T("Foe.Undead.Skeleton"), T("Player") }));
    EXPECT_EQ(actor->transform->GetTags(), (TagContainer{ T("Foe.Boss") }));
    EXPECT_EQ(Names(world->FindActorsWithTag(T("Foe"))), (std::vector<std::string>{ "Skeleton" }));
}

TEST_F(TagWorldTest, ReferencesAreListed) {
    world->GetTags().Add(T("Enemy.Boss"));
    auto boss = Spawn("Boss", { "Enemy.Boss" });
    boss->transform->GetTags().Add(T("Hitbox.Head"));
    Spawn("Hero", { "Player" });

    std::vector<std::string> found;
    Worlds::CollectTagReferences(*world, T("Enemy"), found);
    EXPECT_EQ(found, (std::vector<std::string>{ "World", "Boss" }));

    found.clear();
    Worlds::CollectTagReferences(*world, T("Hitbox"), found);
    EXPECT_EQ(found, (std::vector<std::string>{ "Boss > Transform" }));      // the class name
}

TEST_F(TagWorldTest, ReferencesAreFoundInAFile) {
    const json data = json::parse(R"({
        "name": "w", "tags": ["Enemy.Boss"],
        "actors": [
            { "name": "Zone", "components": [ { "type": "damageZone", "affects": "Player | Enemy & !Status.Dead", "label": "Enemy camp" } ],
              "children": [ { "name": "Grunt", "tags": ["Enemy.Undead.Skeleton"], "components": [] } ] },
            { "name": "Rock", "tags": ["Enemyish"], "components": [] }
        ]
    })");

    std::vector<std::string> found;
    Worlds::CollectTagReferences(data, "Enemy", found);
    EXPECT_EQ(found, (std::vector<std::string>{ "World", "Zone > damageZone.affects", "Grunt" }));
}

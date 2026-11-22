#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "engine/core/collision/collision_profile.hpp"
#include "engine/core/jobs/job_system.hpp"
#include "engine/core/tags/tag_registry.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/components/script.hpp"
#include "support/test_engine_context.hpp"

#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>

using namespace Shard::Engine;
using Shard::Engine::Objects::Actor;
using Shard::Engine::Core::Object;
using Shard::Engine::Objects::Components::PhysicsBody;
using Shard::Engine::Objects::Components::Script;
using Shard::Engine::Physics::PhysicsManager;
using Shard::Engine::Physics::KindBit;
using Shard::Engine::Physics::SubscribeOptions;
namespace Col = Shard::Engine::Collision;

namespace {
    class CollisionWitness : public Script {
        public:
            using Script::Script;
            void OnOverlapBegin(const Events::CollisionEvent& e) override { ++begins; lastOther = e.other; }
            void OnOverlapEnd(const Events::CollisionEvent&) override { ++ends; }
            void OnHit(const Events::CollisionEvent& e) override { ++hits; lastSpeed = e.impactSpeed; }
            int begins = 0;
            int ends = 0;
            int hits = 0;
            float lastSpeed = 0.0f;
            std::shared_ptr<Actor> lastOther;
    };

    Col::Profile Prof(const char* name, std::initializer_list<const char*> identity, std::initializer_list<std::pair<const char*, Col::Response>> rules,
                      Col::Response fallback = Col::Response::Ignore, Col::Mode mode = Col::Mode::Solid)
    {
        Col::Profile p;
        p.name = name;
        for (const char* tag : identity) p.identity.Add(Core::Tag::Request(tag));
        for (const auto& [text, response] : rules) p.rules.push_back({ Core::TagQuery::Parse(text), response });
        p.defaultResponse = fallback;
        p.mode = mode;
        return p;
    }

    class CollisionWorld : public ::testing::Test {
        protected:
            void SetUp() override {
                Core::TagRegistry::Get().Clear();
                Core::TagRegistry::Get().SetDeclarations(Core::TagSource::Project, "", {
                    { "Hero", "" }, { "Ghost", "" }, { "Floor", "" }, { "Trigger", "" }, { "Shot", "" }, { "Status.Frozen", "" }, { "Surface.Metal", "" } });
                Col::ProfileRegistry::Get().Clear();
                Col::ProfileRegistry::Get().SetCollisionRoots(Col::Source::Project, "", { "Hero", "Ghost", "Floor", "Trigger", "Shot" });
                using R = Col::Response;
                auto& registry = Col::ProfileRegistry::Get();
                registry.Declare(Col::Source::Project, "", Prof("Hero", { "Hero" }, { { "Floor | Hero", R::Block }, { "Trigger", R::Overlap } }));
                registry.Declare(Col::Source::Project, "", Prof("Floor", { "Floor" }, { { "Hero | Shot", R::Block }, { "Ghost", R::Overlap } }));
                registry.Declare(Col::Source::Project, "", Prof("Ghost", { "Ghost" }, { { "Floor", R::Overlap } }));
                registry.Declare(Col::Source::Project, "", Prof("Zone", { "Trigger" }, { { "Hero | Ghost", R::Overlap } }, R::Ignore, Col::Mode::Sensor));
                registry.Declare(Col::Source::Project, "", Prof("Shot", { "Shot" }, { { "Floor", R::Block } }));
                registry.Declare(Col::Source::Project, "", Prof("Passer", { "Hero" }, { { "Floor", R::Ignore } }));

                Core::JobSystemDesc desc;
                desc.workerCount = 3;
                jobs = std::make_unique<Core::JobSystem>(desc);
                context.jobSystem = jobs.get();
                Core::SetEngine(&context);
                physics.Init(glm::vec3(0.0f, -9.81f, 0.0f));
                context.physicsManager = &physics;
                context.collisionHost = &physics.Collision();
            }

            void TearDown() override {
                actors.clear();
                physics.Shutdown();
                context.physicsManager = nullptr;
                context.collisionHost = nullptr;
                Core::SetEngine(nullptr);
                jobs.reset();
                Col::ProfileRegistry::Get().Clear();
                Core::TagRegistry::Get().Clear();
            }

            std::shared_ptr<Actor> Spawn(const char* name, const char* profile, glm::vec3 position, glm::vec3 scale, JPH::EMotionType motion) {
                auto actor = Object::CreateWithContext<Actor>(&context, std::string(name), &context);
                actor->transform->SetPosition(position);
                actor->transform->SetScale(scale);
                auto body = actor->AddComponent<PhysicsBody>();
                body->collisionProfile = profile;
                body->CreateBody(motion);
                actors.push_back(actor);
                return actor;
            }

            std::shared_ptr<Actor> Floor() { return Spawn("Floor", "Floor", glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f), JPH::EMotionType::Static); }

            JPH::BodyID IdOf(const std::shared_ptr<Actor>& actor) { return actor->GetComponent<PhysicsBody>()->GetBodyID(); }

            float Height(const std::shared_ptr<Actor>& actor) { return actor->GetComponent<PhysicsBody>()->GetPosition().y; }

            void Steps(int count) { for (int i = 0; i < count; ++i) physics.StepSimulation(1.0f / 60.0f, false); }

            std::vector<Events::CollisionEvent>* Listen(const std::shared_ptr<Actor>& actor, uint32_t kinds, SubscribeOptions options = {}) {
                auto* sink = new std::vector<Events::CollisionEvent>();
                sinks.emplace_back(sink);
                physics.Collision().Subscribe(actor, kinds, std::move(options), [sink](const Events::CollisionEvent& e) { sink->push_back(e); });
                return sink;
            }

            static int Count(const std::vector<Events::CollisionEvent>& events, Col::EventKind kind) {
                int n = 0;
                for (const auto& e : events) if (e.kind == kind) ++n;
                return n;
            }

            Shard::Tests::TestEngineContext context;
            std::unique_ptr<Core::JobSystem> jobs;
            PhysicsManager physics;
            std::vector<std::shared_ptr<Actor>> actors;
            std::vector<std::unique_ptr<std::vector<Events::CollisionEvent>>> sinks;
    };
}

TEST_F(CollisionWorld, SensorSeesAFallingBodyBeginAndEnd) {
    auto zone = Spawn("Zone", "Zone", glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(4.0f, 2.0f, 4.0f), JPH::EMotionType::Static);
    auto hero = Spawn("Hero", "Hero", glm::vec3(0.0f, 9.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    auto* events = Listen(zone, KindBit(Col::EventKind::OverlapBegin) | KindBit(Col::EventKind::OverlapEnd));

    Steps(240);
    EXPECT_EQ(Count(*events, Col::EventKind::OverlapBegin), 1);
    EXPECT_EQ(Count(*events, Col::EventKind::OverlapEnd), 1);
    EXPECT_LT(Height(hero), 3.0f);
    ASSERT_FALSE(events->empty());
    EXPECT_EQ((*events)[0].other, hero);
    EXPECT_EQ(events->back().cause, Col::EndCause::Separated);
}

TEST_F(CollisionWorld, ASensorOnAStaticActorNeverSeesAStaticOne) {
    auto zone = Spawn("Zone", "Zone", glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(4.0f, 2.0f, 4.0f), JPH::EMotionType::Static);
    Floor();
    auto* events = Listen(zone, Physics::kAllCollisionKinds);
    Steps(30);
    EXPECT_TRUE(events->empty());
}

TEST_F(CollisionWorld, OverlapBetweenSolidBodiesPassesThroughAndPairsBeginEnd) {
    Floor();
    auto ghost = Spawn("Ghost", "Ghost", glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    auto* events = Listen(ghost, KindBit(Col::EventKind::OverlapBegin) | KindBit(Col::EventKind::OverlapEnd));

    Steps(240);
    EXPECT_LT(Height(ghost), -1.0f);
    EXPECT_EQ(Count(*events, Col::EventKind::OverlapBegin), 1);
    EXPECT_EQ(Count(*events, Col::EventKind::OverlapEnd), 1);
}

TEST_F(CollisionWorld, BlockingContactGivesAHitWithAnImpactSpeed) {
    Floor();
    auto hero = Spawn("Hero", "Hero", glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    auto* events = Listen(hero, KindBit(Col::EventKind::Hit));

    Steps(180);
    ASSERT_GE(Count(*events, Col::EventKind::Hit), 1);
    const Events::CollisionEvent& hit = (*events)[0];
    EXPECT_GT(hit.impactSpeed, 3.0f);
    EXPECT_GT(hit.normal.y, 0.5f);
    EXPECT_NEAR(Height(hero), 0.5f, 0.1f);
}

TEST_F(CollisionWorld, HitThresholdFiltersGentleContacts) {
    Floor();
    auto hero = Spawn("Hero", "Hero", glm::vec3(0.0f, 0.55f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    SubscribeOptions options;
    options.minImpactSpeed = 50.0f;
    auto* events = Listen(hero, KindBit(Col::EventKind::Hit), options);
    Steps(120);
    EXPECT_EQ(Count(*events, Col::EventKind::Hit), 0);
}

TEST_F(CollisionWorld, NothingIsProducedWithoutAListener) {
    Floor();
    Spawn("Hero", "Hero", glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    Steps(120);
    EXPECT_EQ(physics.Collision().SubscriptionCount(), 0u);
    EXPECT_EQ(physics.Collision().GetRecorder().Size(), 0u);
}

TEST_F(CollisionWorld, AProfileThatIgnoresTheFloorFallsThrough) {
    Floor();
    auto passer = Spawn("Passer", "Passer", glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    Steps(120);
    EXPECT_LT(Height(passer), -2.0f);
}

TEST_F(CollisionWorld, AnOverrideLetsABodyThroughThenGivesTheFloorBack) {
    Floor();
    auto hero = Spawn("Hero", "Hero", glm::vec3(0.0f, 0.55f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    Steps(60);
    EXPECT_NEAR(Height(hero), 0.5f, 0.1f);

    const uint64_t id = physics.Collision().PushOverride(IdOf(hero), "dash", 0, Col::IgnorePatch(Core::TagQuery::Parse("Floor")));
    ASSERT_NE(id, 0u);
    Steps(60);
    EXPECT_LT(Height(hero), -0.5f);

    EXPECT_TRUE(physics.Collision().PopOverride(IdOf(hero), id));
    EXPECT_EQ(physics.Collision().StateOf(IdOf(hero))->overrides.Entries().size(), 0u);
}

TEST_F(CollisionWorld, OverrideEndsAnOverlapWithAProfileChangedCause) {
    auto zone = Spawn("Zone", "Zone", glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(4.0f, 4.0f, 4.0f), JPH::EMotionType::Static);
    Floor();
    auto hero = Spawn("Hero", "Hero", glm::vec3(0.0f, 0.55f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    auto* events = Listen(zone, KindBit(Col::EventKind::OverlapBegin) | KindBit(Col::EventKind::OverlapEnd));
    Steps(30);
    ASSERT_EQ(Count(*events, Col::EventKind::OverlapBegin), 1);

    physics.Collision().PushOverride(IdOf(hero), "ghost", 0, Col::IgnorePatch(Core::TagQuery::Parse("Trigger")));
    Steps(2);
    ASSERT_EQ(Count(*events, Col::EventKind::OverlapEnd), 1);
    EXPECT_EQ(events->back().cause, Col::EndCause::ProfileChanged);
}

TEST_F(CollisionWorld, IgnoreCollisionWithSkipsOnePair) {
    auto floor = Floor();
    auto a = Spawn("A", "Hero", glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    auto c = Spawn("C", "Hero", glm::vec3(5.0f, 2.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    physics.Collision().IgnoreCollision(IdOf(a), IdOf(floor), true);
    EXPECT_TRUE(physics.Collision().IsIgnoring(IdOf(a), IdOf(floor)));
    EXPECT_TRUE(physics.Collision().IsIgnoring(IdOf(floor), IdOf(a)));
    EXPECT_FALSE(physics.Collision().IsIgnoring(IdOf(c), IdOf(floor)));

    Steps(120);
    EXPECT_LT(Height(a), -2.0f);
    EXPECT_NEAR(Height(c), 0.5f, 0.1f);
}

TEST_F(CollisionWorld, DestroyingAnOverlappingActorEndsTheOverlap) {
    auto zone = Spawn("Zone", "Zone", glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(4.0f, 4.0f, 4.0f), JPH::EMotionType::Static);
    Floor();
    auto hero = Spawn("Hero", "Hero", glm::vec3(0.0f, 0.55f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    auto* events = Listen(zone, KindBit(Col::EventKind::OverlapBegin) | KindBit(Col::EventKind::OverlapEnd));
    Steps(30);
    ASSERT_EQ(Count(*events, Col::EventKind::OverlapBegin), 1);
    EXPECT_EQ(physics.Collision().Overlapping(IdOf(zone)).size(), 1u);

    hero->GetComponent<PhysicsBody>()->RemoveBody();
    Steps(1);
    ASSERT_EQ(Count(*events, Col::EventKind::OverlapEnd), 1);
    EXPECT_EQ(events->back().cause, Col::EndCause::Destroyed);
    EXPECT_TRUE(physics.Collision().Overlapping(IdOf(zone)).empty());
}

TEST_F(CollisionWorld, ExplainNamesTheProfilesAndTheRule) {
    auto floor = Floor();
    auto ghost = Spawn("Ghost", "Ghost", glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    const Physics::Explanation why = physics.Collision().Explain(IdOf(ghost), IdOf(floor));
    EXPECT_EQ(why.response, Col::Response::Overlap);
    const std::string text = why.Text();
    EXPECT_NE(text.find("profile 'Ghost'"), std::string::npos);
    EXPECT_NE(text.find("profile 'Floor'"), std::string::npos);
    EXPECT_NE(text.find("rule 1"), std::string::npos);
}

TEST_F(CollisionWorld, QueryAsFollowsTheRulesOfAProfile) {
    auto floor = Floor();
    auto zone = Spawn("Zone", "Zone", glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(2.0f), JPH::EMotionType::Static);
    Steps(1);

    auto cast = [&](const char* profile, Col::Response minimum) {
        Col::ResolvedProfile resolved;
        EXPECT_TRUE(Col::ProfileRegistry::Get().Resolve(profile, resolved));
        const Col::ClassId asClass = physics.Collision().Classes().Acquire(resolved, true);
        Physics::CollisionSystem::RowFilter filter(physics.Collision().Classes(), asClass, minimum);
        JPH::RRayCast ray(JPH::RVec3(0.0, 10.0, 0.0), JPH::Vec3(0.0f, -20.0f, 0.0f));
        JPH::RayCastResult hit;
        JPH::BroadPhaseLayerFilter broad;
        const bool found = physics.GetPhysicsSystem().GetNarrowPhaseQuery().CastRay(ray, hit, broad, filter);
        physics.Collision().Classes().Release(asClass);
        return found ? hit.mBodyID : JPH::BodyID();
    };

    EXPECT_EQ(cast("Shot", Col::Response::Block), IdOf(floor));
    EXPECT_EQ(cast("Hero", Col::Response::Overlap), IdOf(zone));
    EXPECT_EQ(cast("Ghost", Col::Response::Block), JPH::BodyID());
}

TEST_F(CollisionWorld, InteractionTableRunsItsActions) {
    Floor();
    auto shot = Spawn("Shot", "Shot", glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.5f), JPH::EMotionType::Dynamic);
    shot->GetTags().Add(Core::Tag::Request("Shot"));
    Col::InteractionRow row;
    row.name = "shot hits floor";
    row.a = Core::TagQuery::Parse("Shot");
    row.b = Core::TagQuery::Parse("Floor");
    row.event = Col::EventKind::Hit;
    row.actions.push_back({ "AddTag", { { "tag", "Status.Frozen" }, { "target", "self" } } });
    physics.Collision().Interactions().Rows().push_back(row);
    physics.Collision().Interactions().Touch();

    Steps(120);
    EXPECT_TRUE(shot->GetTags().HasTag(Core::Tag::Request("Status.Frozen")));
}

TEST_F(CollisionWorld, RulesOnOtherTagsAreCheckedWhenTheContactStarts) {
    Col::Profile walker = Prof("Walker", { "Hero" }, { { "Status.Frozen", Col::Response::Ignore }, { "Floor", Col::Response::Block } });
    Col::ProfileRegistry::Get().Declare(Col::Source::Project, "", walker);
    Floor();
    auto frozen = Spawn("Frozen", "Walker", glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    auto normal = Spawn("Normal", "Walker", glm::vec3(10.0f, 2.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    Steps(1);
    Steps(120);
    EXPECT_NEAR(Height(normal), 0.5f, 0.1f);
    EXPECT_NEAR(Height(frozen), 0.5f, 0.1f);
}

uint64_t RecordedHash = 0;

TEST_F(CollisionWorld, TheRecordKeepsTheEvents) {
    physics.Collision().SetRecording(true);
    Floor();
    Spawn("Ghost", "Ghost", glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    Steps(240);
    EXPECT_GT(physics.Collision().GetRecorder().Size(), 0u);
    RecordedHash = physics.Collision().GetRecorder().StreamHash();
    EXPECT_NE(RecordedHash, 0u);
}

TEST_F(CollisionWorld, ASecondRunGivesTheSameRecordHash) {
    if (RecordedHash == 0)
        GTEST_SKIP() << "needs TheRecordKeepsTheEvents to run first";
    physics.Collision().SetRecording(true);
    Floor();
    Spawn("Ghost", "Ghost", glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    Steps(240);
    EXPECT_EQ(physics.Collision().GetRecorder().StreamHash(), RecordedHash);
}

TEST_F(CollisionWorld, AScriptReceivesItsCollisionCallbacksAfterListening) {
    auto zone = Spawn("Zone", "Zone", glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(4.0f, 2.0f, 4.0f), JPH::EMotionType::Static);
    auto hero = Spawn("Hero", "Hero", glm::vec3(0.0f, 9.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    auto quiet = zone->AddComponent<CollisionWitness>();
    auto listening = hero->AddComponent<CollisionWitness>();
    EXPECT_NE(listening->ListenCollisions(), 0u);
    auto zoneListener = zone->AddComponent<CollisionWitness>();
    EXPECT_NE(zoneListener->ListenCollisions(Physics::KindBit(Col::EventKind::OverlapBegin) | Physics::KindBit(Col::EventKind::OverlapEnd)), 0u);
    Floor();

    Steps(240);
    EXPECT_EQ(quiet->begins, 0);
    EXPECT_EQ(zoneListener->begins, 1);
    EXPECT_EQ(zoneListener->ends, 1);
    EXPECT_EQ(zoneListener->lastOther, hero);
    EXPECT_EQ(zoneListener->hits, 0);

    EXPECT_GE(listening->hits, 1);
    EXPECT_GT(listening->lastSpeed, 3.0f);

    listening->StopListeningCollisions();
    const int before = listening->hits;
    hero->GetComponent<PhysicsBody>()->SetPositionD(glm::dvec3(0.0, 4.0, 0.0));
    Steps(120);
    EXPECT_EQ(listening->hits, before);
}

TEST_F(CollisionWorld, DefaultProfilesKeepTheOldBehavior) {
    auto ground = Spawn("Ground", "", glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f), JPH::EMotionType::Static);
    auto box = Spawn("Box", "", glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    Steps(180);
    EXPECT_NEAR(Height(box), 0.5f, 0.1f);
}

TEST_F(CollisionWorld, ExplainGivesStructuredSides) {
    auto floor = Floor();
    auto ghost = Spawn("Ghost", "Ghost", glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    const Physics::Explanation why = physics.Collision().Explain(IdOf(ghost), IdOf(floor));
    EXPECT_TRUE(why.sides[0].hasProfile);
    EXPECT_EQ(why.sides[0].profile, "Ghost");
    EXPECT_TRUE(why.sides[0].moving);
    EXPECT_EQ(why.sides[1].profile, "Floor");
    EXPECT_FALSE(why.sides[1].moving);
    EXPECT_FALSE(why.groupIgnored);
    ASSERT_FALSE(why.reasons.empty());
    EXPECT_EQ(why.reasons.front().rfind("first: ", 0), 0u);
}

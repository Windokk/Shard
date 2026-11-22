#include <gtest/gtest.h>

#include "engine/core/collision/collision_classes.hpp"
#include "engine/core/collision/collision_diagnostics.hpp"
#include "engine/core/collision/collision_overrides.hpp"
#include "engine/core/collision/collision_profile.hpp"
#include "engine/core/collision/collision_recorder.hpp"
#include "engine/core/collision/collision_tracker.hpp"
#include "engine/core/collision/interaction_table.hpp"
#include "engine/core/tags/tag_registry.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Collision;
using Shard::Engine::Core::Tag;
using Shard::Engine::Core::TagContainer;
using Shard::Engine::Core::TagQuery;
using Shard::Engine::Core::TagRegistry;
using Shard::Engine::Core::TagSource;

namespace {
    class CollisionTest : public ::testing::Test {
        protected:
            void SetUp() override {
                TagRegistry::Get().Clear();
                TagRegistry::Get().SetDeclarations(TagSource::Project, "", {
                    { "Enemy.Undead.Skeleton", "" },
                    { "Enemy.Boss", "" },
                    { "World.Terrain", "" },
                    { "Player", "" },
                    { "Player.Projectile", "" },
                    { "Pickup", "" },
                    { "Surface.Metal.Steel", "" },
                    { "Surface.Metal", "" },
                    { "Impact.Heavy", "" },
                    { "Status.Frozen", "" },
                });
                ProfileRegistry::Get().Clear();
                ProfileRegistry::Get().SetCollisionRoots(Source::Project, "", { "Enemy", "World", "Player", "Pickup" });
            }

            void TearDown() override {
                ProfileRegistry::Get().Clear();
                TagRegistry::Get().Clear();
            }

            static Tag T(const char* name) { return Tag::Request(name); }
            static TagQuery Q(const char* text) { return TagQuery::Parse(text); }

            static Profile Make(const char* name, std::initializer_list<const char*> identity, std::initializer_list<std::pair<const char*, Response>> rules, Response fallback = Response::Ignore)
            {
                Profile p;
                p.name = name;
                for (const char* tag : identity) p.identity.Add(T(tag));
                for (const auto& [text, response] : rules) p.rules.push_back({ Q(text), response });
                p.defaultResponse = fallback;
                return p;
            }

            static ClassId Acquire(ClassTable& table, const char* profile, bool moving)
            {
                ResolvedProfile resolved;
                std::string error;
                EXPECT_TRUE(ProfileRegistry::Get().Resolve(profile, resolved, &error)) << error;
                return table.Acquire(resolved, moving);
            }
    };
}

TEST_F(CollisionTest, DefaultProfilesKeepTheOldTwoLayerMatrix) {
    ClassTable table;
    const ClassId fixed = Acquire(table, kDefaultStaticProfile, false);
    const ClassId moving = Acquire(table, kDefaultMovingProfile, true);
    EXPECT_EQ(table.PairResponse(fixed, moving), Response::Block);
    EXPECT_EQ(table.PairResponse(moving, moving), Response::Block);
    EXPECT_EQ(table.PairResponse(fixed, fixed), Response::Ignore);
}

TEST_F(CollisionTest, InheritanceMergesIdentityAndPutsOwnRulesFirst) {
    auto& registry = ProfileRegistry::Get();
    registry.Declare(Source::Project, "", Make("Character", { "Player" }, { { "World", Response::Block } }));
    Profile child = Make("Hero", { "Pickup" }, { { "World", Response::Ignore } });
    child.base = "Character";
    registry.Declare(Source::Project, "", child);

    ResolvedProfile resolved;
    ASSERT_TRUE(registry.Resolve("Hero", resolved));
    EXPECT_TRUE(resolved.identity.HasTag(T("Player")));
    EXPECT_TRUE(resolved.identity.HasTag(T("Pickup")));
    ASSERT_EQ(resolved.rules.size(), 2u);
    EXPECT_EQ(resolved.rules[0].response, Response::Ignore);
    EXPECT_EQ(resolved.rules[1].response, Response::Block);
}

TEST_F(CollisionTest, InheritanceCycleAndUnknownBaseAreReported) {
    auto& registry = ProfileRegistry::Get();
    Profile a = Make("A", {}, {});
    a.base = "B";
    Profile b = Make("B", {}, {});
    b.base = "A";
    Profile c = Make("C", {}, {});
    c.base = "Nope";
    registry.Declare(Source::Project, "", a);
    registry.Declare(Source::Project, "", b);
    registry.Declare(Source::Project, "", c);

    ResolvedProfile out;
    std::string error;
    EXPECT_FALSE(registry.Resolve("A", out, &error));
    EXPECT_NE(error.find("inherits from itself"), std::string::npos);
    EXPECT_FALSE(registry.Resolve("C", out, &error));
    EXPECT_NE(error.find("unknown base"), std::string::npos);
    EXPECT_FALSE(registry.Resolve("Missing", out, &error));
}

TEST_F(CollisionTest, ProjectProfileWinsOverPluginAndCode) {
    auto& registry = ProfileRegistry::Get();
    Profile plugin = Make("Shared", { "Player" }, {});
    plugin.material = "from_plugin";
    Profile project = Make("Shared", { "Player" }, {});
    project.material = "from_project";
    registry.Declare(Source::Plugin, "some.plugin", plugin);
    registry.Declare(Source::Project, "", project);
    EXPECT_EQ(registry.Find("Shared")->material, "from_project");
    registry.RemoveDeclarations(Source::Project, "");
    EXPECT_EQ(registry.Find("Shared")->material, "from_plugin");
}

TEST_F(CollisionTest, IdentityKeepsOnlyCollisionTags) {
    auto& registry = ProfileRegistry::Get();
    registry.Declare(Source::Project, "", Make("Mixed", { "Player", "Status.Frozen" }, {}));
    ResolvedProfile resolved;
    ASSERT_TRUE(registry.Resolve("Mixed", resolved));
    EXPECT_TRUE(resolved.identity.HasTag(T("Player")));
    EXPECT_FALSE(resolved.identity.HasTag(T("Status.Frozen")));
}

TEST_F(CollisionTest, FirstMatchingRuleWinsAndHierarchyApplies) {
    auto& registry = ProfileRegistry::Get();
    registry.Declare(Source::Project, "", Make("Skeleton", { "Enemy.Undead.Skeleton" }, {
        { "Enemy.Boss", Response::Block }, { "Enemy", Response::Ignore }, { "World", Response::Block } }));
    registry.Declare(Source::Project, "", Make("Boss", { "Enemy.Boss" }, { { "Enemy", Response::Block } }));
    registry.Declare(Source::Project, "", Make("Ground", { "World.Terrain" }, { { "Enemy", Response::Block } }));

    ClassTable table;
    const ClassId skeleton = Acquire(table, "Skeleton", true);
    const ClassId boss = Acquire(table, "Boss", true);
    const ClassId ground = Acquire(table, "Ground", false);
    EXPECT_EQ(table.SideResponse(skeleton, boss), Response::Block);
    EXPECT_EQ(table.PairResponse(skeleton, boss), Response::Block);
    EXPECT_EQ(table.PairResponse(skeleton, ground), Response::Block);

    const ClassId other = Acquire(table, "Skeleton", false);
    EXPECT_EQ(other == skeleton, false);
    EXPECT_EQ(table.PairResponse(skeleton, skeleton), Response::Ignore);
}

TEST_F(CollisionTest, PairTakesTheWeakerSide) {
    auto& registry = ProfileRegistry::Get();
    registry.Declare(Source::Project, "", Make("Hero", { "Player" }, { { "Pickup", Response::Block } }));
    registry.Declare(Source::Project, "", Make("Coin", { "Pickup" }, { { "Player", Response::Overlap } }));
    registry.Declare(Source::Project, "", Make("Wall", { "World" }, { { "Player", Response::Block } }));
    registry.Declare(Source::Project, "", Make("Ghost", { "Enemy" }, { { "Player", Response::Block } }));

    ClassTable table;
    const ClassId hero = Acquire(table, "Hero", true);
    const ClassId coin = Acquire(table, "Coin", true);
    EXPECT_EQ(table.PairResponse(hero, coin), Response::Overlap);
    EXPECT_EQ(table.PairResponse(coin, hero), Response::Overlap);

    const ClassId ghost = Acquire(table, "Ghost", true);
    EXPECT_EQ(table.PairResponse(hero, ghost), Response::Ignore);
}

TEST_F(CollisionTest, SensorNeverBlocksAndStaticSensorNeedsAMover) {
    auto& registry = ProfileRegistry::Get();
    Profile trigger = Make("Trigger", { "Pickup" }, { { "Player", Response::Block } });
    trigger.mode = Mode::Sensor;
    registry.Declare(Source::Project, "", trigger);
    registry.Declare(Source::Project, "", Make("Hero", { "Player" }, { { "Pickup", Response::Block } }));

    ClassTable table;
    const ClassId hero = Acquire(table, "Hero", true);
    const ClassId fixedHero = Acquire(table, "Hero", false);
    const ClassId staticTrigger = Acquire(table, "Trigger", false);
    const ClassId movingTrigger = Acquire(table, "Trigger", true);
    EXPECT_EQ(table.PairResponse(staticTrigger, hero), Response::Overlap);
    EXPECT_EQ(table.PairResponse(staticTrigger, fixedHero), Response::Ignore);
    EXPECT_EQ(table.PairResponse(movingTrigger, fixedHero), Response::Overlap);
    EXPECT_EQ(table.PairResponse(movingTrigger, movingTrigger), Response::Ignore);
}

TEST_F(CollisionTest, DisabledAndQueryOnlyNeverInteract) {
    auto& registry = ProfileRegistry::Get();
    Profile off = Make("Off", { "Player" }, { { "World", Response::Block } });
    off.mode = Mode::Disabled;
    Profile query = Make("Query", { "Pickup" }, { { "World", Response::Block } });
    query.mode = Mode::QueryOnly;
    registry.Declare(Source::Project, "", off);
    registry.Declare(Source::Project, "", query);
    registry.Declare(Source::Project, "", Make("Wall", { "World" }, { { "Player | Pickup", Response::Block } }));

    ClassTable table;
    const ClassId wall = Acquire(table, "Wall", false);
    EXPECT_EQ(table.PairResponse(wall, Acquire(table, "Off", true)), Response::Ignore);
    EXPECT_EQ(table.PairResponse(wall, Acquire(table, "Query", true)), Response::Ignore);
}

TEST_F(CollisionTest, RulesOnOtherTagsAreDecidedAtContactTime) {
    auto& registry = ProfileRegistry::Get();
    registry.Declare(Source::Project, "", Make("Walker", { "Enemy" }, { { "Status.Frozen", Response::Ignore }, { "World", Response::Block } }));
    registry.Declare(Source::Project, "", Make("Ground", { "World" }, { { "Enemy", Response::Block } }));

    ClassTable table;
    const ClassId walker = Acquire(table, "Walker", true);
    const ClassId ground = Acquire(table, "Ground", false);
    EXPECT_TRUE(table.PairNeedsContact(walker, ground));
    EXPECT_EQ(table.PairResponse(walker, ground), Response::Block);

    const TagContainer plain = table.Get(ground).identity;
    TagContainer frozen = plain;
    frozen.Add(T("Status.Frozen"));
    const TagContainer walkerTags = table.Get(walker).identity;
    EXPECT_EQ(table.Decide(walker, ground, &walkerTags, &plain).response, Response::Block);
    EXPECT_EQ(table.Decide(walker, ground, &walkerTags, &frozen).response, Response::Ignore);
}

TEST_F(CollisionTest, ClassesAreSharedCountedAndRecycled) {
    auto& registry = ProfileRegistry::Get();
    registry.Declare(Source::Project, "", Make("A", { "Player" }, { { "World", Response::Block } }));
    registry.Declare(Source::Project, "", Make("B", { "Enemy" }, { { "World", Response::Block } }));

    ClassTable table;
    const ClassId first = Acquire(table, "A", true);
    EXPECT_EQ(Acquire(table, "A", true), first);
    EXPECT_EQ(table.Get(first).refs, 2u);
    table.Release(first);
    EXPECT_TRUE(table.IsAlive(first));
    table.Release(first);
    EXPECT_FALSE(table.IsAlive(first));

    const ClassId reused = Acquire(table, "B", true);
    EXPECT_EQ(reused, first);
    EXPECT_EQ(table.AliveCount(), 1u);
}

TEST_F(CollisionTest, TooManyClassesNamesTheProfile) {
    auto& registry = ProfileRegistry::Get();
    registry.Declare(Source::Project, "", Make("A", { "Player" }, {}));
    registry.Declare(Source::Project, "", Make("B", { "Enemy" }, {}));
    ClassTable table(1);
    EXPECT_NE(Acquire(table, "A", true), kInvalidClass);
    ResolvedProfile resolved;
    ASSERT_TRUE(registry.Resolve("B", resolved));
    std::string error;
    EXPECT_EQ(table.Acquire(resolved, true, &error), kInvalidClass);
    EXPECT_NE(error.find("'B'"), std::string::npos);
}

TEST_F(CollisionTest, ExplainNamesTheRuleThatDecided) {
    auto& registry = ProfileRegistry::Get();
    registry.Declare(Source::Project, "", Make("Skeleton", { "Enemy.Undead.Skeleton" }, { { "Enemy", Response::Ignore }, { "World", Response::Block } }));
    registry.Declare(Source::Project, "", Make("Boss", { "Enemy.Boss" }, { { "Enemy", Response::Block } }));

    ClassTable table;
    const ClassId skeleton = Acquire(table, "Skeleton", true);
    const ClassId boss = Acquire(table, "Boss", true);
    const Decision d = table.Decide(skeleton, boss, nullptr, nullptr, true);
    EXPECT_EQ(d.response, Response::Ignore);
    bool named = false;
    for (const std::string& line : d.reasons)
        named = named || line.find("rule 1 (Enemy)") != std::string::npos;
    EXPECT_TRUE(named);
}

TEST_F(CollisionTest, OverridesFoldByPriorityAndGiveBack) {
    auto& registry = ProfileRegistry::Get();
    registry.Declare(Source::Project, "", Make("Hero", { "Player" }, { { "Enemy", Response::Block } }));
    ResolvedProfile base;
    ASSERT_TRUE(registry.Resolve("Hero", base));

    OverrideStack stack;
    const uint64_t dash = stack.Push("dash", 0, IgnorePatch(Q("Enemy")));
    Patch loud;
    loud.rules.push_back({ Q("Enemy.Boss"), Response::Block });
    loud.mode = Mode::Sensor;
    stack.Push("boss", 5, loud);

    ResolvedProfile folded = stack.Fold(base);
    ASSERT_EQ(folded.rules.size(), 3u);
    EXPECT_EQ(folded.rules[0].target.ToString(), "Enemy.Boss");
    EXPECT_EQ(folded.mode, Mode::Sensor);

    EXPECT_TRUE(stack.Pop(dash));
    EXPECT_FALSE(stack.Pop(dash));
    EXPECT_EQ(stack.RemoveOwner("boss"), 1u);
    EXPECT_TRUE(stack.Empty());
    EXPECT_EQ(stack.Fold(base).rules.size(), 1u);
}

TEST_F(CollisionTest, OverridePatchTurnsABlockIntoAnIgnore) {
    auto& registry = ProfileRegistry::Get();
    registry.Declare(Source::Project, "", Make("Hero", { "Player" }, { { "Enemy", Response::Block } }));
    registry.Declare(Source::Project, "", Make("Orc", { "Enemy" }, { { "Player", Response::Block } }));
    ResolvedProfile base;
    ASSERT_TRUE(registry.Resolve("Hero", base));

    ClassTable table;
    const ClassId orc = Acquire(table, "Orc", true);
    const ClassId hero = table.Acquire(base, true);
    EXPECT_EQ(table.PairResponse(hero, orc), Response::Block);

    OverrideStack stack;
    stack.Push("dash", 0, IgnorePatch(Q("Enemy")));
    const ClassId dashing = table.Acquire(stack.Fold(base), true);
    EXPECT_NE(dashing, hero);
    EXPECT_EQ(table.PairResponse(dashing, orc), Response::Ignore);
}

TEST_F(CollisionTest, TrackerPairsEveryBeginWithAnEnd) {
    OverlapTracker tracker;
    std::vector<TrackerEvent> events;

    tracker.Add(5, 2, 10, 20, events);
    tracker.Add(2, 5, 21, 11, events);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, EventKind::OverlapBegin);
    EXPECT_EQ(events[0].a, 2u);
    EXPECT_TRUE(tracker.Overlaps(5, 2));

    events.clear();
    tracker.Remove(2, 5, 20, 10, events);
    EXPECT_TRUE(events.empty());
    tracker.Remove(5, 2, 11, 21, events);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, EventKind::OverlapEnd);
    EXPECT_EQ(events[0].cause, EndCause::Separated);
    EXPECT_FALSE(tracker.Tracks(2));
}

TEST_F(CollisionTest, TrackerFlushesWithACauseWhenAColliderGoesAway) {
    OverlapTracker tracker;
    std::vector<TrackerEvent> events;
    tracker.Add(1, 2, 0, 0, events);
    tracker.Add(1, 3, 0, 0, events);
    tracker.Add(4, 5, 0, 0, events);
    events.clear();

    tracker.RemoveCollider(1, EndCause::Destroyed, events);
    ASSERT_EQ(events.size(), 2u);
    for (const TrackerEvent& e : events) {
        EXPECT_EQ(e.kind, EventKind::OverlapEnd);
        EXPECT_EQ(e.cause, EndCause::Destroyed);
    }
    EXPECT_EQ(tracker.PairCount(), 1u);
    EXPECT_TRUE(tracker.Overlapping(1).empty());
    EXPECT_EQ(tracker.Overlapping(4), std::vector<uint64_t>({ 5 }));
}

TEST_F(CollisionTest, TrackerIgnoresRemovalOfContactsItNeverSaw) {
    OverlapTracker tracker;
    std::vector<TrackerEvent> events;
    tracker.Remove(1, 2, 3, 4, events);
    EXPECT_TRUE(events.empty());
}

TEST_F(CollisionTest, RecorderKeepsTheLastRecordsOnly) {
    Recorder recorder(sizeof(Record) * 16);
    for (uint64_t i = 0; i < 40; ++i) {
        Record r;
        r.step = i;
        r.kind = RecordKind::Event;
        r.a = i;
        recorder.Push(r);
    }
    EXPECT_EQ(recorder.Size(), 16u);
    EXPECT_EQ(recorder.FirstStep(), 24u);
    EXPECT_EQ(recorder.LastStep(), 39u);
    EXPECT_EQ(recorder.Step(30).size(), 1u);
    EXPECT_TRUE(recorder.Step(3).empty());
}

TEST_F(CollisionTest, RecorderHashFollowsTheEventStream) {
    auto fill = [](Recorder& r, uint64_t b) {
        for (uint64_t i = 0; i < 5; ++i) {
            Record e;
            e.step = i;
            e.kind = RecordKind::Event;
            e.a = i;
            e.b = i == 3 ? b : i;
            r.Push(e);
        }
    };
    Recorder one, two, three;
    fill(one, 3);
    fill(two, 3);
    fill(three, 99);
    EXPECT_EQ(one.StreamHash(), two.StreamHash());
    EXPECT_NE(one.StreamHash(), three.StreamHash());
}

TEST_F(CollisionTest, InteractionTablePrefersTheMostSpecificTagAndFallsBackUp) {
    InteractionTable table;
    InteractionRow general;
    general.name = "metal";
    general.a = Q("Impact.Heavy");
    general.b = Q("Surface.Metal");
    general.minSpeed = 3.0f;
    general.actions.push_back({ "PlaySound", { { "name", "clang" } } });
    InteractionRow specific = general;
    specific.name = "steel";
    specific.b = Q("Surface.Metal.Steel");
    specific.actions = { { "PlaySound", { { "name", "ring" } } } };
    table.Rows().push_back(general);
    table.Rows().push_back(specific);

    const TagContainer impact({ T("Impact.Heavy") });
    const TagContainer steel({ T("Surface.Metal.Steel") });
    const TagContainer metal({ T("Surface.Metal") });

    EXPECT_EQ(table.Find(impact, steel, EventKind::Hit, 5.0f).row->name, "steel");
    EXPECT_EQ(table.Find(impact, metal, EventKind::Hit, 5.0f).row->name, "metal");
    EXPECT_EQ(table.Find(impact, steel, EventKind::Hit, 1.0f).row, nullptr);
    EXPECT_EQ(table.Find(impact, steel, EventKind::OverlapBegin, 5.0f).row, nullptr);

    const InteractionMatch swapped = table.Find(steel, impact, EventKind::Hit, 5.0f);
    ASSERT_NE(swapped.row, nullptr);
    EXPECT_TRUE(swapped.swapped);
    EXPECT_EQ(swapped.row->actions[0].Param("name") ? *swapped.row->actions[0].Param("name") : "", "ring");
}

TEST_F(CollisionTest, ActionParametersParse) {
    Action action{ "Damage", { { "amount", "2.5" }, { "kind", "Fire" } } };
    EXPECT_FLOAT_EQ(action.ParamFloat("amount", 0.0f), 2.5f);
    EXPECT_FLOAT_EQ(action.ParamFloat("missing", 7.0f), 7.0f);
    EXPECT_FLOAT_EQ(action.ParamFloat("kind", 1.0f), 1.0f);
}

TEST_F(CollisionTest, DiagnosticsFindDeadShadowedIsolatedAndOneSided) {
    auto& registry = ProfileRegistry::Get();
    registry.Declare(Source::Project, "", Make("Skeleton", { "Enemy.Undead.Skeleton" }, {
        { "Enemy", Response::Ignore }, { "Enemy.Boss", Response::Block }, { "Pickup", Response::Block }, { "Nobody.Here", Response::Block } }));
    registry.Declare(Source::Project, "", Make("Boss", { "Enemy.Boss" }, {}));
    registry.Declare(Source::Project, "", Make("Coin", { "Pickup" }, { { "Player", Response::Overlap } }));
    registry.Declare(Source::Project, "", Make("Hero", { "Player" }, { { "Pickup", Response::Overlap } }));
    registry.Declare(Source::Project, "", Make("Lonely", { "World.Terrain" }, {}));

    const std::vector<Diagnostic> found = Analyze(registry);
    auto has = [&](DiagnosticKind kind, const char* profile, int rule = -1) {
        for (const Diagnostic& d : found)
            if (d.kind == kind && d.profile == profile && (rule < 0 || d.rule == rule))
                return true;
        return false;
    };
    EXPECT_TRUE(has(DiagnosticKind::ShadowedRule, "Skeleton", 2));
    EXPECT_TRUE(has(DiagnosticKind::DeadRule, "Skeleton", 4));
    EXPECT_TRUE(has(DiagnosticKind::IsolatedProfile, "Lonely"));
    EXPECT_FALSE(has(DiagnosticKind::IsolatedProfile, "Hero"));
    bool oneSided = false;
    for (const Diagnostic& d : found)
        oneSided = oneSided || (d.kind == DiagnosticKind::OneSidedPair && d.profile == "Skeleton" && d.other == "Coin");
    EXPECT_TRUE(oneSided);
}

TEST_F(CollisionTest, DiagnosticsFlagListenersThatCanNeverFire) {
    auto& registry = ProfileRegistry::Get();
    registry.Declare(Source::Project, "", Make("Door", { "World" }, { { "Player", Response::Block } }));
    registry.Declare(Source::Project, "", Make("Hero", { "Player" }, { { "World", Response::Block } }));

    ListenerSpec wantsOverlap;
    wantsOverlap.owner = "Door";
    wantsOverlap.profile = "Door";
    wantsOverlap.kind = EventKind::OverlapBegin;
    wantsOverlap.other = Q("Player");
    EXPECT_EQ(AnalyzeListener(registry, wantsOverlap).size(), 1u);

    ListenerSpec wantsHit = wantsOverlap;
    wantsHit.kind = EventKind::Hit;
    EXPECT_TRUE(AnalyzeListener(registry, wantsHit).empty());
}

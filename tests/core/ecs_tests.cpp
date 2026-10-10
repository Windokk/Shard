#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "engine/core/ecs/registry.hpp"
#include "engine/core/ecs/scheduler.hpp"

using namespace Shard::Engine::Core;
using namespace Shard::Engine::Core::Ecs;

namespace {
    struct Position { float x = 0, y = 0, z = 0; };
    struct Velocity { float x = 0, y = 0, z = 0; };
    struct Health { int value = 100; };
    struct Dead { uint8_t tag = 1; };
    struct Big { char bytes[9000]; };           // two do not fit in one 16 KB chunk
}

// ------------------------------------------------------------------------------------------------ entities

TEST(EcsEntity, NullAndPacking) {
    Entity none;
    EXPECT_TRUE(none.IsNull());
    const Entity e = Entity::Make(7, 3);
    EXPECT_EQ(e.Index(), 7u);
    EXPECT_EQ(e.Generation(), 3u);
    EXPECT_FALSE(e.IsNull());
    EXPECT_EQ(sizeof(Entity), 8u);
}

TEST(EcsRegistry, StaleEntityIsDeadAfterSlotReuse) {
    Registry registry;
    const Entity a = registry.Create(Health{5});
    EXPECT_TRUE(registry.IsAlive(a));
    EXPECT_TRUE(registry.Destroy(a));
    EXPECT_FALSE(registry.IsAlive(a));
    EXPECT_FALSE(registry.Destroy(a));                       // already gone

    const Entity b = registry.Create(Health{9});
    EXPECT_EQ(b.Index(), a.Index());                         // same slot, recycled
    EXPECT_NE(b.Generation(), a.Generation());
    EXPECT_FALSE(registry.IsAlive(a));                       // the old id does not point at the new entity
    EXPECT_EQ(registry.TryGet<Health>(a), nullptr);
    EXPECT_EQ(registry.Get<Health>(b).value, 9);
}

TEST(EcsRegistry, CreateWithComponentsAndGet) {
    Registry registry;
    const Entity e = registry.Create(Position{1, 2, 3}, Health{42});
    EXPECT_TRUE(registry.Has<Position>(e));
    EXPECT_TRUE(registry.Has<Health>(e));
    EXPECT_FALSE(registry.Has<Velocity>(e));
    EXPECT_EQ(registry.Get<Position>(e).y, 2.0f);
    registry.Get<Health>(e).value = 7;
    EXPECT_EQ(registry.Get<Health>(e).value, 7);
    EXPECT_EQ(registry.Count(), 1u);
}

TEST(EcsRegistry, AddAndRemoveMoveTheEntityKeepingItsOtherData) {
    Registry registry;
    const Entity e = registry.Create(Position{1, 2, 3});
    EXPECT_TRUE(registry.Add(e, Velocity{4, 5, 6}));
    EXPECT_EQ(registry.Get<Position>(e).z, 3.0f);            // survived the move
    EXPECT_EQ(registry.Get<Velocity>(e).x, 4.0f);

    EXPECT_TRUE(registry.Remove<Position>(e));
    EXPECT_FALSE(registry.Has<Position>(e));
    EXPECT_EQ(registry.Get<Velocity>(e).z, 6.0f);
    EXPECT_FALSE(registry.Remove<Position>(e));              // not there anymore

    EXPECT_TRUE(registry.Add(e, Velocity{9, 9, 9}));         // overwrite, no move
    EXPECT_EQ(registry.Get<Velocity>(e).x, 9.0f);
}

TEST(EcsRegistry, SwapRemoveKeepsEveryOtherEntityIntact) {
    // Far more than one chunk, destroy in a scattered order, and check each survivor still has ITS data
    Registry registry;
    std::vector<Entity> entities;
    for (int i = 0; i < 5000; ++i) entities.push_back(registry.Create(Health{i}, Position{float(i), 0, 0}));

    for (int i = 0; i < 5000; i += 3) EXPECT_TRUE(registry.Destroy(entities[i]));
    for (int i = 0; i < 5000; ++i) {
        if (i % 3 == 0) { EXPECT_FALSE(registry.IsAlive(entities[i])); continue; }
        ASSERT_TRUE(registry.IsAlive(entities[i]));
        EXPECT_EQ(registry.Get<Health>(entities[i]).value, i);
        EXPECT_EQ(registry.Get<Position>(entities[i]).x, float(i));
    }
    EXPECT_EQ(registry.CountOf<Health>(), 5000u - 1667u);
}

TEST(EcsRegistry, ComponentsLargerThanHalfAChunkStillWork) {
    Registry registry;
    std::vector<Entity> entities;
    for (int i = 0; i < 5; ++i) {
        Big big{};
        big.bytes[0] = char('a' + i);
        entities.push_back(registry.Create(big));
    }
    for (int i = 0; i < 5; ++i) EXPECT_EQ(registry.Get<Big>(entities[i]).bytes[0], char('a' + i));
}

// ------------------------------------------------------------------------------------------------ queries

TEST(EcsQuery, EachVisitsOnlyMatchingEntities) {
    Registry registry;
    for (int i = 0; i < 10; ++i) registry.Create(Position{float(i), 0, 0}, Velocity{1, 0, 0});
    for (int i = 0; i < 7; ++i) registry.Create(Position{});                       // no velocity
    for (int i = 0; i < 3; ++i) registry.Create(Velocity{});                       // no position

    int visited = 0;
    registry.Each<Position, const Velocity>([&](Position& p, const Velocity& v) { p.x += v.x; ++visited; });
    EXPECT_EQ(visited, 10);

    float sum = 0;
    registry.Each<const Position>([&](Entity, const Position& p) { sum += p.x; });
    EXPECT_FLOAT_EQ(sum, (0 + 9) * 10 / 2.0f + 10);                                // moved by 1 each
}

TEST(EcsQuery, ExcludeSkipsEntitiesWithTheComponent) {
    Registry registry;
    for (int i = 0; i < 4; ++i) registry.Create(Health{i});
    for (int i = 0; i < 6; ++i) registry.Create(Health{i}, Dead{});

    int alive = 0;
    registry.EachExcluding<Exclude<Dead>, const Health>([&](const Health&) { ++alive; });
    EXPECT_EQ(alive, 4);
}

TEST(EcsQuery, ChunkLevelAccessGivesContiguousColumns) {
    Registry registry;
    for (int i = 0; i < 3000; ++i) registry.Create(Position{float(i), 0, 0}, Velocity{2, 0, 0});

    size_t total = 0;
    registry.EachChunk<Position, const Velocity>([&](size_t n, const Entity*, Position* p, const Velocity* v) {
        for (size_t i = 0; i < n; ++i) p[i].x += v[i].x;
        total += n;
    });
    EXPECT_EQ(total, 3000u);

    double sum = 0;
    registry.Each<const Position>([&](const Position& p) { sum += p.x; });
    EXPECT_DOUBLE_EQ(sum, 3000.0 * 2999.0 / 2.0 + 3000.0 * 2.0);
}

TEST(EcsQuery, ParallelEachMatchesSerial) {
    JobSystem js;
    Registry registry;
    for (int i = 0; i < 20000; ++i) registry.Create(Position{float(i), 0, 0}, Velocity{1, 2, 3});

    registry.ParallelEach<Position, const Velocity>(&js, [](Position& p, const Velocity& v) { p.x += v.x; p.y += v.y; });

    double sumX = 0, sumY = 0;
    registry.Each<const Position>([&](const Position& p) { sumX += p.x; sumY += p.y; });
    EXPECT_DOUBLE_EQ(sumX, 20000.0 * 19999.0 / 2.0 + 20000.0);
    EXPECT_DOUBLE_EQ(sumY, 40000.0);
}

TEST(EcsQuery, StructuralChangesAreRefusedWhileIteratingButValuesCanChange) {
    Registry registry;
    const Entity a = registry.Create(Health{1});
    const Entity b = registry.Create(Health{2});

    registry.Each<Health>([&](Entity e, Health& h) {
        EXPECT_TRUE(registry.IsIterating());
        EXPECT_TRUE(registry.Create().IsNull());                       // refused
        EXPECT_FALSE(registry.Destroy(e));
        EXPECT_FALSE(registry.Add(e, Velocity{}));                     // would move the entity
        EXPECT_FALSE(registry.Remove<Health>(e));
        EXPECT_TRUE(registry.Add(e, Health{h.value + 10}));            // overwriting a value is not structural
    });
    EXPECT_FALSE(registry.IsIterating());
    EXPECT_EQ(registry.Get<Health>(a).value, 11);
    EXPECT_EQ(registry.Get<Health>(b).value, 12);
    EXPECT_EQ(registry.Count(), 2u);
}

// ------------------------------------------------------------------------------------------------ hierarchy

TEST(EcsHierarchy, ParentChildOrderAndQueries) {
    Registry registry;
    const Entity root = registry.Create();
    const Entity a = registry.Create(), b = registry.Create(), c = registry.Create();
    EXPECT_TRUE(registry.SetParent(a, root));
    EXPECT_TRUE(registry.SetParent(b, root));
    EXPECT_TRUE(registry.SetParent(c, a));

    EXPECT_EQ(registry.GetParent(a), root);
    EXPECT_EQ(registry.ChildCount(root), 2u);
    EXPECT_EQ(registry.FirstChild(root), a);                           // attach order
    EXPECT_EQ(registry.NextSibling(a), b);
    EXPECT_TRUE(registry.IsAncestor(root, c));
    EXPECT_FALSE(registry.IsAncestor(b, c));

    std::vector<Entity> order;
    registry.ForEachDescendant(root, [&](Entity e) { order.push_back(e); });
    EXPECT_EQ(order, (std::vector<Entity>{a, c, b}));                  // parents before children, depth first
}

TEST(EcsHierarchy, ReparentDetachAndCycleRefusal) {
    Registry registry;
    const Entity a = registry.Create(), b = registry.Create(), c = registry.Create();
    EXPECT_TRUE(registry.SetParent(b, a));
    EXPECT_TRUE(registry.SetParent(c, b));

    EXPECT_FALSE(registry.SetParent(a, c));                            // a would be its own ancestor
    EXPECT_FALSE(registry.SetParent(a, a));
    EXPECT_EQ(registry.GetParent(a), kNullEntity);

    EXPECT_TRUE(registry.SetParent(c, a));                             // reparent
    EXPECT_EQ(registry.ChildCount(b), 0u);
    EXPECT_EQ(registry.ChildCount(a), 2u);
    EXPECT_TRUE(registry.SetParent(c, kNullEntity));                   // detach
    EXPECT_EQ(registry.GetParent(c), kNullEntity);
    EXPECT_EQ(registry.ChildCount(a), 1u);
}

TEST(EcsHierarchy, DestroyingAParentDestroysItsDescendants) {
    Registry registry;
    const Entity root = registry.Create(Health{1});
    const Entity child = registry.Create(Health{2});
    const Entity grandchild = registry.Create(Health{3});
    const Entity bystander = registry.Create(Health{4});
    registry.SetParent(child, root);
    registry.SetParent(grandchild, child);

    EXPECT_TRUE(registry.Destroy(root));
    EXPECT_FALSE(registry.IsAlive(child));
    EXPECT_FALSE(registry.IsAlive(grandchild));
    EXPECT_TRUE(registry.IsAlive(bystander));
    EXPECT_EQ(registry.Get<Health>(bystander).value, 4);
    EXPECT_EQ(registry.Count(), 1u);
}

TEST(EcsHierarchy, DestroyingAChildUnlinksItFromTheParent) {
    Registry registry;
    const Entity root = registry.Create();
    const Entity a = registry.Create(), b = registry.Create(), c = registry.Create();
    registry.SetParent(a, root); registry.SetParent(b, root); registry.SetParent(c, root);
    registry.Destroy(b);
    EXPECT_EQ(registry.ChildCount(root), 2u);
    EXPECT_EQ(registry.NextSibling(a), c);
}

// ------------------------------------------------------------------------------------------------ command buffers

TEST(EcsCommandBuffer, RecordsNowAppliesLater) {
    Registry registry;
    const Entity e = registry.Create(Health{1});

    CommandBuffer commands;
    commands.Add(e, Velocity{3, 0, 0});
    commands.Remove<Health>(e);
    const Entity spawned = commands.Create();                          // placeholder
    commands.Add(spawned, Position{5, 5, 5});
    commands.SetParent(spawned, e);
    EXPECT_EQ(commands.Size(), 5u);
    EXPECT_FALSE(registry.Has<Velocity>(e));                           // nothing happened yet

    const auto result = registry.Playback(commands);
    EXPECT_EQ(result.applied, 5u);
    EXPECT_EQ(result.skipped, 0u);
    EXPECT_TRUE(commands.Empty());
    EXPECT_TRUE(registry.Has<Velocity>(e));
    EXPECT_FALSE(registry.Has<Health>(e));
    EXPECT_EQ(registry.ChildCount(e), 1u);
    EXPECT_EQ(registry.Get<Position>(registry.FirstChild(e)).y, 5.0f);   // the placeholder became a real entity
}

TEST(EcsCommandBuffer, CommandsOnDeadEntitiesAreSkipped) {
    Registry registry;
    const Entity e = registry.Create(Health{1});
    CommandBuffer commands;
    commands.Add(e, Velocity{});
    commands.Destroy(e);
    commands.Add(e, Position{});                                       // e is dead by now
    const auto result = registry.Playback(commands);
    EXPECT_EQ(result.applied, 2u);
    EXPECT_EQ(result.skipped, 1u);
    EXPECT_FALSE(registry.IsAlive(e));
}

TEST(EcsCommandBuffer, RecordedDuringIterationAppliedAfter) {
    Registry registry;
    for (int i = 0; i < 100; ++i) registry.Create(Health{i});

    CommandBuffer commands;
    registry.Each<Health>([&](Entity e, Health& h) { if (h.value % 2 == 0) commands.Destroy(e); else commands.Add(e, Dead{}); });
    EXPECT_EQ(registry.Count(), 100u);
    registry.Playback(commands);
    EXPECT_EQ(registry.Count(), 50u);
    EXPECT_EQ(registry.CountOf<Dead>(), 50u);
}

// ------------------------------------------------------------------------------------------------ scheduler

TEST(EcsScheduler, PhasesRunInOrderAndFixedRunsPerStep) {
    Registry registry;
    Scheduler scheduler;
    std::vector<std::string> log;
    auto add = [&](const char* name, Phase phase) {
        SystemDesc d; d.name = name; d.phase = phase;
        d.run = [&log, name](SystemContext& c) { log.push_back(std::string(name) + (std::string(name) == "fixed" ? std::to_string(c.fixedStep) : "")); };
        EXPECT_NE(scheduler.Add(std::move(d)), Scheduler::kInvalid);
    };
    // registered in the "wrong" order on purpose
    add("extract", Phase::Extract); add("late", Phase::Late); add("update", Phase::Update);
    add("post", Phase::PostPhysics); add("fixed", Phase::Fixed); add("presim", Phase::PreSim); add("input", Phase::Input);

    Scheduler::FrameTiming timing; timing.fixedSteps = 3;
    scheduler.RunFrame(registry, nullptr, timing);
    EXPECT_EQ(log, (std::vector<std::string>{"input", "presim", "fixed0", "fixed1", "fixed2", "post", "update", "late", "extract"}));

    log.clear();
    timing.fixedSteps = 0;                                              // a fast frame : no fixed step owed
    scheduler.RunFrame(registry, nullptr, timing);
    EXPECT_EQ(log, (std::vector<std::string>{"input", "presim", "post", "update", "late", "extract"}));
}

TEST(EcsScheduler, ConflictingSystemsKeepRegistrationOrderIndependentOnesAreFree) {
    Scheduler scheduler;
    auto add = [&](const char* name, std::vector<ComponentId> reads, std::vector<ComponentId> writes) {
        SystemDesc d; d.name = name; d.phase = Phase::Update; d.reads = reads; d.writes = writes; d.run = [](SystemContext&) {};
        scheduler.Add(std::move(d));
    };
    const ComponentId P = ComponentIdOf<Position>(), V = ComponentIdOf<Velocity>(), H = ComponentIdOf<Health>();
    add("move", {V}, {P});          // writes P
    add("damage", {}, {H});         // independent of the others
    add("render_prep", {P}, {});    // reads P : must come after move
    EXPECT_EQ(scheduler.Order(Phase::Update), (std::vector<std::string>{"move", "damage", "render_prep"}));
}

TEST(EcsScheduler, ExplicitAfterAndBeforeReorder) {
    Scheduler scheduler;
    auto add = [&](const char* name, std::vector<std::string> after, std::vector<std::string> before) {
        SystemDesc d; d.name = name; d.phase = Phase::Update; d.after = after; d.before = before; d.run = [](SystemContext&) {};
        scheduler.Add(std::move(d));
    };
    add("a", {"c"}, {});            // a after c
    add("b", {}, {"a"});            // b before a
    add("c", {}, {});
    EXPECT_EQ(scheduler.Order(Phase::Update), (std::vector<std::string>{"b", "c", "a"}));
    EXPECT_TRUE(scheduler.Validate());
}

TEST(EcsScheduler, CyclesAndUnknownNamesAreReportedAndDoNotBreakTheRun) {
    Registry registry;
    Scheduler scheduler;
    int ran = 0;
    auto add = [&](const char* name, std::vector<std::string> after) {
        SystemDesc d; d.name = name; d.phase = Phase::Update; d.after = after; d.run = [&ran](SystemContext&) { ++ran; };
        scheduler.Add(std::move(d));
    };
    add("x", {"y"});
    add("y", {"x"});
    add("z", {"ghost"});
    std::string error;
    EXPECT_FALSE(scheduler.Validate(&error));
    EXPECT_NE(error.find("cycle"), std::string::npos);
    EXPECT_NE(error.find("ghost"), std::string::npos);
    scheduler.RunFrame(registry, nullptr, {});
    EXPECT_EQ(ran, 3);                                                  // constraints ignored, everything still runs
}

TEST(EcsScheduler, DuplicateNamesAndEmptySystemsAreRejected) {
    Scheduler scheduler;
    SystemDesc a; a.name = "dup"; a.run = [](SystemContext&) {};
    SystemDesc b = a;
    SystemDesc none; none.name = "none";
    EXPECT_NE(scheduler.Add(a), Scheduler::kInvalid);
    EXPECT_EQ(scheduler.Add(b), Scheduler::kInvalid);
    EXPECT_EQ(scheduler.Add(none), Scheduler::kInvalid);
}

TEST(EcsScheduler, DisableAndRemove) {
    Registry registry;
    Scheduler scheduler;
    int a = 0, b = 0;
    SystemDesc da; da.name = "a"; da.run = [&a](SystemContext&) { ++a; };
    SystemDesc db; db.name = "b"; db.run = [&b](SystemContext&) { ++b; };
    scheduler.Add(da); scheduler.Add(db);
    scheduler.RunFrame(registry, nullptr, {});
    scheduler.SetEnabled("a", false);
    scheduler.RunFrame(registry, nullptr, {});
    scheduler.Remove("b");
    scheduler.RunFrame(registry, nullptr, {});
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 2);
    EXPECT_FALSE(scheduler.Has("b"));
    EXPECT_FALSE(scheduler.SetEnabled("b", true));
}

TEST(EcsScheduler, SystemsCanQueryAndStructuralChangesApplyAtEndOfPhase) {
    Registry registry;
    for (int i = 0; i < 10; ++i) registry.Create(Health{i});

    Scheduler scheduler;
    size_t seenByLaterSystem = 0;
    SystemDesc killer; killer.name = "killer"; killer.phase = Phase::Update; killer.Writes<Health>();
    killer.run = [](SystemContext& c) { c.registry.Each<Health>([&c](Entity e, Health& h) { if (h.value < 5) c.commands.Destroy(e); }); };
    SystemDesc counter; counter.name = "counter"; counter.phase = Phase::Update; counter.Reads<Health>();
    counter.run = [&seenByLaterSystem](SystemContext& c) { seenByLaterSystem = c.registry.CountOf<Health>(); };
    scheduler.Add(killer); scheduler.Add(counter);

    scheduler.RunFrame(registry, nullptr, {});
    EXPECT_EQ(seenByLaterSystem, 10u);                                  // the destroys were deferred past the phase
    EXPECT_EQ(registry.Count(), 5u);                                    // ... and applied at its end
}

TEST(EcsScheduler, IndependentSystemsReallyRunAtTheSameTime) {
    JobSystemDesc desc; desc.workerCount = 3;
    JobSystem js(desc);
    Registry registry;
    Scheduler scheduler;

    // Each waits (bounded) for the other to have started : only possible if both run concurrently
    std::atomic<int> started{0};
    std::atomic<bool> overlapped[2] = {false, false};
    for (int i = 0; i < 2; ++i) {
        SystemDesc d; d.name = "s" + std::to_string(i); d.phase = Phase::Update;
        if (i == 0) d.Writes<Position>(); else d.Writes<Velocity>();
        d.run = [&, i](SystemContext&) {
            started.fetch_add(1);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (started.load() < 2 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
            overlapped[i] = started.load() >= 2;
        };
        scheduler.Add(std::move(d));
    }
    scheduler.RunFrame(registry, &js, {});
    EXPECT_TRUE(overlapped[0]);
    EXPECT_TRUE(overlapped[1]);
}

TEST(EcsScheduler, ConflictingSystemsNeverOverlapAndKeepTheirOrder) {
    JobSystemDesc desc; desc.workerCount = 3;
    JobSystem js(desc);
    Registry registry;
    Scheduler scheduler;

    std::atomic<int> inside{0};
    std::atomic<int> maxInside{0};
    std::vector<int> order;                                             // protected by the scheduler's own ordering
    for (int i = 0; i < 6; ++i) {
        SystemDesc d; d.name = "w" + std::to_string(i); d.phase = Phase::Update; d.Writes<Position>();
        d.run = [&, i](SystemContext&) {
            const int now = inside.fetch_add(1) + 1;
            int seen = maxInside.load();
            while (now > seen && !maxInside.compare_exchange_weak(seen, now)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            order.push_back(i);
            inside.fetch_sub(1);
        };
        scheduler.Add(std::move(d));
    }
    scheduler.RunFrame(registry, &js, {});
    EXPECT_EQ(maxInside.load(), 1);
    EXPECT_EQ(order, (std::vector<int>{0, 1, 2, 3, 4, 5}));
}

TEST(EcsScheduler, ParallelAndSerialGiveTheSameWorld) {
    // Many systems spawning entities through their command buffers : the result must not depend on thread timing
    auto run = [](JobSystem* js) {
        Registry registry;
        Scheduler scheduler;
        for (int i = 0; i < 8; ++i) {
            SystemDesc d; d.name = "spawn" + std::to_string(i); d.phase = Phase::Update;
            d.run = [i](SystemContext& c) {
                for (int k = 0; k < 20; ++k) { const Entity e = c.commands.Create(); c.commands.Add(e, Health{i * 100 + k}); }
            };
            scheduler.Add(std::move(d));
        }
        scheduler.RunFrame(registry, js, {});
        std::vector<int> values;
        registry.Each<const Health>([&](const Health& h) { values.push_back(h.value); });
        return values;
    };
    JobSystemDesc desc; desc.workerCount = 3;
    JobSystem js(desc);
    const std::vector<int> serial = run(nullptr);
    ASSERT_EQ(serial.size(), 160u);
    for (int rep = 0; rep < 20; ++rep) EXPECT_EQ(run(&js), serial);
}

TEST(EcsScheduler, ParallelSystemsCanUseParallelEach) {
    JobSystem js;
    Registry registry;
    for (int i = 0; i < 5000; ++i) registry.Create(Position{0, 0, 0}, Velocity{1, 1, 1}, Health{1});

    Scheduler scheduler;
    SystemDesc move; move.name = "move"; move.phase = Phase::Fixed; move.Reads<Velocity>().Writes<Position>();
    move.run = [](SystemContext& c) { c.registry.ParallelEach<Position, const Velocity>(c.jobs, [](Position& p, const Velocity& v) { p.x += v.x; }); };
    SystemDesc heal; heal.name = "heal"; heal.phase = Phase::Fixed; heal.Writes<Health>();
    heal.run = [](SystemContext& c) { c.registry.ParallelEach<Health>(c.jobs, [](Health& h) { h.value += 1; }); };
    scheduler.Add(move); scheduler.Add(heal);

    Scheduler::FrameTiming timing; timing.fixedSteps = 4;
    scheduler.RunFrame(registry, &js, timing);

    double x = 0, h = 0;
    registry.Each<const Position, const Health>([&](const Position& p, const Health& health) { x += p.x; h += health.value; });
    EXPECT_DOUBLE_EQ(x, 5000.0 * 4);
    EXPECT_DOUBLE_EQ(h, 5000.0 * 5);
}

// ------------------------------------------------------------------------------------------------ deferred destroy, stats

TEST(EcsRegistry, DestroyDeferredWaitsForTheIterationToEnd) {
    Registry registry;
    std::vector<Entity> entities;
    for (int i = 0; i < 10; ++i) entities.push_back(registry.Create(Health{i}));

    registry.Each<Health>([&](Entity e, Health& h) { if (h.value % 2 == 0) registry.DestroyDeferred(e); });
    EXPECT_EQ(registry.Count(), 10u);                                  // nothing destroyed under the iteration
    EXPECT_EQ(registry.GetStats().pendingDestroys, 5u);

    EXPECT_EQ(registry.FlushDeferred(), 5u);
    EXPECT_EQ(registry.Count(), 5u);
    EXPECT_EQ(registry.GetStats().pendingDestroys, 0u);

    registry.DestroyDeferred(entities[1]);                             // nothing running : immediate
    EXPECT_FALSE(registry.IsAlive(entities[1]));
}

TEST(EcsRegistry, DeferredDestroyOfAnAlreadyDeadDescendantIsHarmless) {
    Registry registry;
    const Entity parent = registry.Create(Health{1}), child = registry.Create(Health{2});
    registry.SetParent(child, parent);
    registry.Each<Health>([&](Entity e, Health&) { registry.DestroyDeferred(e); });     // both queued, parent first
    EXPECT_EQ(registry.FlushDeferred(), 1u);                                            // the child went with its parent
    EXPECT_EQ(registry.Count(), 0u);
}

TEST(EcsRegistry, StatsAndDescribe) {
    Registry registry;
    for (int i = 0; i < 3; ++i) registry.Create(Health{i});
    registry.Create(Health{}, Position{});
    const Registry::Stats stats = registry.GetStats();
    EXPECT_EQ(stats.entities, 4u);
    EXPECT_GE(stats.archetypes, 3u);                                   // empty, {Health}, {Health, Position}
    EXPECT_EQ(stats.chunks, 2u);
    EXPECT_GT(stats.chunkBytes, 0u);
    const std::string text = registry.Describe();
    EXPECT_NE(text.find("3 entities"), std::string::npos);
    EXPECT_NE(text.find("1 entity"), std::string::npos);
}

TEST(EcsScheduler, ReportsTimingsAndAFlushAtTheEndOfEveryPhase) {
    Registry registry;
    Scheduler scheduler;
    const Entity doomed = registry.Create(Health{1});

    SystemDesc d; d.name = "sleepy"; d.phase = Phase::Update;
    d.run = [doomed](SystemContext& c) {
        c.registry.Each<Health>([&](Health&) { c.registry.DestroyDeferred(doomed); });
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    };
    scheduler.Add(d);
    SystemDesc off; off.name = "off"; off.phase = Phase::Late; off.run = [](SystemContext&) {};
    scheduler.Add(off);
    scheduler.SetEnabled("off", false);

    scheduler.RunFrame(registry, nullptr, {});
    EXPECT_FALSE(registry.IsAlive(doomed));                            // flushed when the phase ended

    const auto stats = scheduler.Stats();
    ASSERT_EQ(stats.size(), 2u);
    EXPECT_EQ(stats[0].name, "sleepy");
    EXPECT_TRUE(stats[0].enabled);
    EXPECT_GE(stats[0].lastMs, 2.0);
    EXPECT_EQ(stats[1].name, "off");
    EXPECT_FALSE(stats[1].enabled);
    EXPECT_NE(scheduler.Describe().find("sleepy"), std::string::npos);
}

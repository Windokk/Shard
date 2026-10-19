#include <gtest/gtest.h>

#include "engine/plugins/extensions.hpp"
#include "engine/plugins/hooks.hpp"
#include "engine/plugins/services.hpp"
#include "engine/world/event_system.hpp"

using namespace Shard::Engine::Plugins;
using namespace Shard::Engine;

// ---- Hooks ----

TEST(PluginHooks, RunByPriorityThenOwnerThenOrder)
{
    HookRegistry hooks;
    std::string order;
    hooks.On<Hooks::PrePlay>(Priority::Late, [&](Hooks::PrePlay&) { order += "L"; }, 1);
    hooks.On<Hooks::PrePlay>(Priority::Normal, [&](Hooks::PrePlay&) { order += "N2"; }, 2);
    hooks.On<Hooks::PrePlay>(Priority::Normal, [&](Hooks::PrePlay&) { order += "N1"; }, 1);
    hooks.On<Hooks::PrePlay>(Priority::Earliest, [&](Hooks::PrePlay&) { order += "E"; }, 3);
    hooks.On<Hooks::PrePlay>(Priority::Normal, [&](Hooks::PrePlay&) { order += "N1b"; }, 1);
    Hooks::PrePlay hook;
    EXPECT_TRUE(hooks.Emit(hook));
    EXPECT_EQ(order, "EN1N1bN2L");
}

TEST(PluginHooks, CancelStopsTheThingButNotTheOtherHandlers)
{
    HookRegistry hooks;
    bool lateSawIt = false;
    hooks.On<Hooks::PreWorldLoad>(Priority::Early, [](Hooks::PreWorldLoad& h) { if (h.worldName == "x.world") h.Cancel("nope"); }, 4);
    hooks.On<Hooks::PreWorldLoad>(Priority::Late, [&](Hooks::PreWorldLoad& h) { lateSawIt = h.cancelled; }, 5);

    Hooks::PreWorldLoad ok("y.world");
    EXPECT_TRUE(hooks.Emit(ok));
    Hooks::PreWorldLoad bad("x.world");
    EXPECT_FALSE(hooks.Emit(bad));
    EXPECT_EQ(bad.reason, "nope");
    EXPECT_EQ(bad.cancelledBy, 4u);
    EXPECT_TRUE(lateSawIt);
}

TEST(PluginHooks, RemoveByTokenAndByOwner)
{
    HookRegistry hooks;
    int a = 0, b = 0, c = 0;
    auto ta = hooks.On<Hooks::PostPlay>(Priority::Normal, [&](Hooks::PostPlay&) { ++a; }, 1);
    hooks.On<Hooks::PostPlay>(Priority::Normal, [&](Hooks::PostPlay&) { ++b; }, 2);
    hooks.On<Hooks::PostPlay>(Priority::Normal, [&](Hooks::PostPlay&) { ++c; }, 2);
    Hooks::PostPlay hook;
    hooks.Emit(hook);
    EXPECT_EQ(a + b + c, 3);
    EXPECT_TRUE(hooks.Remove(ta));
    EXPECT_FALSE(hooks.Remove(ta));
    EXPECT_EQ(hooks.RemoveOwner(2), 2u);
    hooks.Emit(hook);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(hooks.Count(), 0u);
}

TEST(PluginHooks, AHandlerMayRemoveALaterOneWhileRunning)
{
    HookRegistry hooks;
    int ran = 0;
    HookRegistry::Token second = 0;
    hooks.On<Hooks::PreFrame>(Priority::Early, [&](Hooks::PreFrame&) { hooks.Remove(second); }, 1);
    second = hooks.On<Hooks::PreFrame>(Priority::Late, [&](Hooks::PreFrame&) { ++ran; }, 1);
    Hooks::PreFrame hook(1);
    hooks.Emit(hook);
    EXPECT_EQ(ran, 0);
}

// ---- Services ----

namespace {
    struct IGreeter { virtual ~IGreeter() = default; virtual int Value() const = 0; };
    struct Greeter : IGreeter { int Value() const override { return 42; } };
}

TEST(PluginServices, ProvideGetAndVersion)
{
    ServiceRegistry services;
    Greeter greeter;
    EXPECT_TRUE(services.Provide("greet", *SemVer::Parse("1.2.0"), static_cast<void*>(&greeter), 3));
    EXPECT_FALSE(services.Provide("greet", *SemVer::Parse("1.0.0"), static_cast<void*>(&greeter), 4));

    auto ref = services.Get<Greeter>("greet", ">=1.0");
    ASSERT_TRUE(ref);
    EXPECT_EQ(ref->Value(), 42);
    EXPECT_FALSE(services.Get<Greeter>("greet", "^2.0"));
    EXPECT_FALSE(services.Get<Greeter>("absent"));
    EXPECT_TRUE(services.Has("greet"));
}

TEST(PluginServices, ARefBecomesEmptyWhenTheProviderGoes)
{
    ServiceRegistry services;
    Greeter greeter;
    services.Provide("greet", *SemVer::Parse("1.0.0"), static_cast<void*>(&greeter), 3);
    auto ref = services.Get<Greeter>("greet");
    ASSERT_TRUE(ref);
    EXPECT_FALSE(services.Revoke("greet", 99));           // not the owner
    EXPECT_TRUE(ref);
    EXPECT_EQ(services.RevokeOwner(3), 1u);
    EXPECT_FALSE(ref);
    EXPECT_EQ(ref.Get(), nullptr);
    EXPECT_FALSE(services.Has("greet"));
}

TEST(PluginServices, ListIsSorted)
{
    ServiceRegistry services;
    Greeter g;
    services.Provide("b.service", SemVer{ 1, 0, 0, {} }, &g, 1);
    services.Provide("a.service", SemVer{ 2, 0, 0, {} }, &g, 1);
    const auto list = services.List();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].name, "a.service");
}

// ---- Extension points ----

namespace {
    struct Feature { int id; };
    struct Other { int id; };
}

TEST(PluginExtensions, AddGetByTypeAndRemove)
{
    ExtensionRegistry reg;
    EXPECT_TRUE(reg.Add<Feature>("render.feature", "bloom", std::make_shared<Feature>(Feature{ 1 }), 7));
    EXPECT_FALSE(reg.Add<Feature>("render.feature", "bloom", std::make_shared<Feature>(Feature{ 2 }), 8));       // same name on the same point
    EXPECT_TRUE(reg.Add<Other>("render.feature", "other", std::make_shared<Other>(Other{ 3 }), 8));

    auto features = reg.Get<Feature>("render.feature");
    ASSERT_EQ(features.size(), 1u);
    EXPECT_EQ(features[0]->id, 1);
    EXPECT_EQ(reg.List("render.feature").size(), 2u);

    EXPECT_TRUE(reg.Remove("render.feature", "bloom", 7));
    EXPECT_FALSE(reg.Remove("render.feature", "bloom", 7));
    EXPECT_EQ(reg.RemoveOwner(8), 1u);
    EXPECT_TRUE(reg.List().empty());
}

TEST(PluginExtensions, ListenersHearThePastAndTheFuture)
{
    ExtensionRegistry reg;
    reg.Add<Feature>("p", "first", std::make_shared<Feature>(Feature{ 1 }), 1);
    std::vector<std::string> log;
    const uint32_t id = reg.Listen("p", [&](const ExtensionRegistry::Item& item, bool added) { log.push_back((added ? "+" : "-") + item.name); });
    reg.Add<Feature>("p", "second", std::make_shared<Feature>(Feature{ 2 }), 2);
    reg.Add<Feature>("q", "elsewhere", std::make_shared<Feature>(Feature{ 3 }), 2);
    reg.Remove("p", "first", 1);
    reg.Unlisten(id);
    reg.Remove("p", "second", 2);
    EXPECT_EQ(log, (std::vector<std::string>{ "+first", "+second", "-first" }));
}

// ---- EventDispatcher subscriptions ----

namespace {
    struct PingEvent : Events::Event {
        int value;
        explicit PingEvent(int v) : Events::Event(Core::ObjectID(0)), value(v) {}
    };
}

TEST(EventDispatcherSubscriptions, UnsubscribeByToken)
{
    Events::EventDispatcher d;
    int a = 0, b = 0;
    auto ta = d.subscribeGlobal<PingEvent>([&](const PingEvent& e) { a += e.value; });
    d.subscribeGlobal<PingEvent>([&](const PingEvent& e) { b += e.value; });
    d.emitGlobal(PingEvent(1));
    EXPECT_TRUE(d.Unsubscribe(ta));
    EXPECT_FALSE(d.Unsubscribe(ta));
    d.emitGlobal(PingEvent(10));
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 11);
}

TEST(EventDispatcherSubscriptions, UnsubscribeByOwnerAcrossScopes)
{
    Events::EventDispatcher d;
    int global = 0, world = 0, kept = 0;
    d.subscribeGlobal<PingEvent>([&](const PingEvent&) { ++global; }, 5);
    d.subscribeToWorld<PingEvent>(1, [&](const PingEvent&) { ++world; }, 5);
    d.subscribeGlobal<PingEvent>([&](const PingEvent&) { ++kept; }, 6);
    d.subscribeGlobal<PingEvent>([&](const PingEvent&) { ++kept; });        // owner 0 : nobody's, never swept
    EXPECT_EQ(d.UnsubscribeOwner(5), 2u);
    EXPECT_EQ(d.UnsubscribeOwner(0), 0u);
    d.emitGlobal(PingEvent(1));
    d.emitToWorld(1, PingEvent(1));
    EXPECT_EQ(global, 0);
    EXPECT_EQ(world, 0);
    EXPECT_EQ(kept, 2);
}

TEST(EventDispatcherSubscriptions, AHandlerMayUnsubscribeItselfAndOthersWhileEmitting)
{
    Events::EventDispatcher d;
    int first = 0, second = 0, third = 0;
    Events::EventDispatcher::Subscription t2 = 0;
    auto t1 = d.subscribeGlobal<PingEvent>([&](const PingEvent&) { ++first; });
    (void)t1;
    t2 = d.subscribeGlobal<PingEvent>([&](const PingEvent&) { ++second; });
    d.subscribeGlobal<PingEvent>([&](const PingEvent&) { ++third; d.Unsubscribe(t2); });

    d.emitGlobal(PingEvent(1));
    d.emitGlobal(PingEvent(1));
    EXPECT_EQ(first, 2);
    EXPECT_EQ(second, 1);          // removed by the third handler during the first emit, after it had run
    EXPECT_EQ(third, 2);
}

TEST(EventDispatcherSubscriptions, SubscribingWhileEmittingDoesNotInvalidateTheLoop)
{
    Events::EventDispatcher d;
    int added = 0;
    bool subscribed = false;
    d.subscribeGlobal<PingEvent>([&](const PingEvent&) {
        if (!subscribed) {
            subscribed = true;
            for (int i = 0; i < 64; ++i) d.subscribeGlobal<PingEvent>([&](const PingEvent&) { ++added; });
        }
    });
    d.emitGlobal(PingEvent(1));
    d.emitGlobal(PingEvent(1));
    EXPECT_GE(added, 64);
}

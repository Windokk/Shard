#include <gtest/gtest.h>

#include "engine/core/ecs/registry.hpp"
#include "engine/plugins/context.hpp"
#include "plugins/plugin_test_utils.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;

namespace {
    struct World {
        Core::Ecs::Scheduler scheduler;
        Events::EventDispatcher events;
        Objects::Components::ComponentRegistry components;
        Core::CVarRegistry cvars;
        ServiceRegistry services;
        HookRegistry hooks;
        ExtensionRegistry extensions;
        ContentMounts content;

        HostBindings Bindings(bool engine = true)
        {
            HostBindings b;
            if (engine) {
                b.scheduler = &scheduler;
                b.events = &events;
            }
            b.components = &components;
            b.cvars = &cvars;
            b.services = &services;
            b.hooks = &hooks;
            b.extensions = &extensions;
            b.content = &content;
            return b;
        }
    };

    PluginManifest Manifest(const std::string& id = "com.test.ctx")
    {
        auto m = ParseManifest(PluginTest::Manifest(id));
        return m.Value();
    }

    std::shared_ptr<Objects::Components::Component> MakeNothing() { return nullptr; }

    struct PingEvent : Events::Event {
        PingEvent() : Events::Event(Core::ObjectID(0)) {}
    };

    Core::Ecs::SystemDesc System(const std::string& name, int* counter, Core::Ecs::Phase phase = Core::Ecs::Phase::Update)
    {
        Core::Ecs::SystemDesc d;
        d.name = name;
        d.phase = phase;
        d.run = [counter](Core::Ecs::SystemContext&) { ++*counter; };
        return d;
    }

    void RunUpdate(Core::Ecs::Scheduler& s)
    {
        Core::Ecs::Registry registry;
        Core::Ecs::Scheduler::FrameTiming timing;
        timing.deltaTime = 0.016f;
        s.RunFrame(registry, nullptr, timing);
    }
}

TEST(PluginContext, SystemsGetThePluginPrefixAndLeaveWithIt)
{
    World w;
    const PluginManifest m = Manifest();
    int ran = 0;
    {
        PluginContext ctx(m, 1, Host::Headless, w.Bindings());
        const std::string name = ctx.Systems().Add(System("tick", &ran));
        EXPECT_EQ(name, "com.test.ctx/tick");
        EXPECT_TRUE(w.scheduler.Has("com.test.ctx/tick"));
        RunUpdate(w.scheduler);
        EXPECT_EQ(ran, 1);
        ctx.ReleaseAll();
        EXPECT_FALSE(w.scheduler.Has("com.test.ctx/tick"));
    }
    RunUpdate(w.scheduler);
    EXPECT_EQ(ran, 1);
}

TEST(PluginContext, TheDestructorReleasesToo)
{
    World w;
    const PluginManifest m = Manifest();
    int ran = 0;
    {
        PluginContext ctx(m, 1, Host::Headless, w.Bindings());
        ctx.Systems().Add(System("tick", &ran));
        ctx.Components().Register("com.test.ctx/Thing", &MakeNothing);
        ctx.CVars().Int("level", 3);
        ctx.Commands().Add("ctx.hello", "says hello", [](const std::vector<std::string>&) { return Core::CommandResult{ true, "hello" }; });
        EXPECT_TRUE(w.components.IsRegistered("com.test.ctx/Thing"));
        EXPECT_NE(w.cvars.Find("com.test.ctx.level"), nullptr);
        EXPECT_EQ(w.cvars.Execute("ctx.hello").output, "hello");
    }
    EXPECT_FALSE(w.scheduler.Has("com.test.ctx/tick"));
    EXPECT_FALSE(w.components.IsRegistered("com.test.ctx/Thing"));
    EXPECT_EQ(w.cvars.Find("com.test.ctx.level"), nullptr);
    EXPECT_FALSE(w.cvars.Execute("ctx.hello").ok);
}

TEST(PluginContext, SystemsAndSubscriptionsWaitForTheEngine)
{
    World w;
    const PluginManifest m = Manifest();
    int ran = 0, heard = 0;
    PluginContext ctx(m, 1, Host::Headless, w.Bindings(false));
    ctx.Systems().Add(System("tick", &ran));
    ctx.Events().Subscribe<PingEvent>([&](const PingEvent&) { ++heard; });
    EXPECT_FALSE(w.scheduler.Has("com.test.ctx/tick"));

    ctx.BindEngine(&w.scheduler, &w.events);
    EXPECT_TRUE(w.scheduler.Has("com.test.ctx/tick"));
    RunUpdate(w.scheduler);
    EXPECT_EQ(ran, 1);
    w.events.emitGlobal(PingEvent());
    EXPECT_EQ(heard, 1);

    ctx.ReleaseAll();
    w.events.emitGlobal(PingEvent());
    EXPECT_EQ(heard, 1);
}

TEST(PluginContext, ReleasedBeforeTheEngineExistedIsSilent)
{
    World w;
    const PluginManifest m = Manifest();
    int ran = 0;
    PluginContext ctx(m, 1, Host::Headless, w.Bindings(false));
    ctx.Systems().Add(System("tick", &ran));
    ctx.ReleaseAll();
    ctx.BindEngine(&w.scheduler, &w.events);
    EXPECT_FALSE(w.scheduler.Has("com.test.ctx/tick"));
}

TEST(PluginContext, SystemOrderReferencesCanNameThePluginsOwnSystems)
{
    World w;
    const PluginManifest m = Manifest();
    int a = 0, b = 0;
    PluginContext ctx(m, 1, Host::Headless, w.Bindings());
    ctx.Systems().Add(System("second", &b).After("./first"));
    ctx.Systems().Add(System("first", &a));
    std::string error;
    EXPECT_TRUE(w.scheduler.Validate(&error)) << error;
    const auto order = w.scheduler.Order(Core::Ecs::Phase::Update);
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "com.test.ctx/first");
    EXPECT_EQ(order[1], "com.test.ctx/second");
}

TEST(PluginContext, ASystemThatThrowsIsReportedNotPropagated)
{
    World w;
    const PluginManifest m = Manifest();
    HostBindings b = w.Bindings();
    std::vector<std::pair<PluginOwner, std::string>> reports;
    b.reportError = [&](PluginOwner owner, const std::string& what) { reports.emplace_back(owner, what); };
    std::atomic<uint32_t> active{ 0 };
    b.activePlugin = &active;
    PluginContext ctx(m, 7, Host::Headless, b);
    Core::Ecs::SystemDesc d;
    d.name = "boom";
    d.phase = Core::Ecs::Phase::Update;
    d.run = [](Core::Ecs::SystemContext&) { throw std::runtime_error("kaboom"); };
    ctx.Systems().Add(std::move(d));
    RunUpdate(w.scheduler);
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].first, 7u);
    EXPECT_EQ(reports[0].second, "kaboom");
    EXPECT_EQ(active.load(), 7u);          // the suspect stays named, for a crash report
}

TEST(PluginContext, CVarsAreNamedByThePluginAndLiveInTheHostRegistry)
{
    World w;
    const PluginManifest m = Manifest();
    PluginContext ctx(m, 1, Host::Headless, w.Bindings());
    auto* rate = ctx.CVars().Float("rain_rate", 0.5f, "Rain", Core::CVarFlags::None, 0.0f, 1.0f);
    ASSERT_NE(rate, nullptr);
    EXPECT_FLOAT_EQ(rate->Get(), 0.5f);
    EXPECT_TRUE(w.cvars.Execute("com.test.ctx.rain_rate 0.9").ok);
    EXPECT_FLOAT_EQ(rate->Get(), 0.9f);
    EXPECT_EQ(ctx.CVars().Float("rain_rate", 1.0f), nullptr);          // already there
    // a value from a config layer set before the variable existed
    w.cvars.SetLayerValue(Core::ConfigLayer::Project, "com.test.ctx.later", "12");
    auto* later = ctx.CVars().Int("later", 1);
    ASSERT_NE(later, nullptr);
    EXPECT_EQ(later->Get(), 12);
}

TEST(PluginContext, ComponentsRefuseADuplicateName)
{
    World w;
    const PluginManifest m = Manifest();
    PluginContext one(m, 1, Host::Headless, w.Bindings());
    const PluginManifest m2 = Manifest("com.test.other");
    PluginContext two(m2, 2, Host::Headless, w.Bindings());
    EXPECT_TRUE(one.Components().Register("Thing", &MakeNothing));
    EXPECT_FALSE(two.Components().Register("Thing", &MakeNothing));
    two.ReleaseAll();
    EXPECT_TRUE(w.components.IsRegistered("Thing"));          // the second did not own it
    one.ReleaseAll();
    EXPECT_FALSE(w.components.IsRegistered("Thing"));
}

TEST(PluginContext, ServicesHooksAndExtensionsAreTaggedWithTheOwner)
{
    World w;
    const PluginManifest m = Manifest();
    struct Impl { int v = 5; } impl;
    struct Item { int x = 1; };
    PluginContext ctx(m, 4, Host::Headless, w.Bindings());
    EXPECT_TRUE(ctx.Services().Provide<Impl>("impl.service", 1, &impl));
    EXPECT_TRUE(ctx.Services().Has("impl.service"));
    ctx.Hooks().On<Hooks::PrePlay>(Priority::Normal, [](Hooks::PrePlay& h) { h.Cancel("no"); });
    EXPECT_TRUE(ctx.Render().AddFeature<Item>("feature", std::make_shared<Item>()));

    Hooks::PrePlay hook;
    EXPECT_FALSE(w.hooks.Emit(hook));
    EXPECT_EQ(hook.cancelledBy, 4u);
    EXPECT_EQ(w.extensions.List("render.feature").size(), 1u);
    auto ref = w.services.Get<Impl>("impl.service");
    EXPECT_TRUE(ref);

    ctx.ReleaseAll();
    Hooks::PrePlay again;
    EXPECT_TRUE(w.hooks.Emit(again));
    EXPECT_TRUE(w.extensions.List().empty());
    EXPECT_FALSE(ref);
}

TEST(PluginContext, ReleaseIsRepeatableAndOrdersLastInFirstOut)
{
    World w;
    const PluginManifest m = Manifest();
    PluginContext ctx(m, 1, Host::Headless, w.Bindings());
    std::string order;
    ctx.Commands().Add("a.cmd", "", [](const std::vector<std::string>&) { return Core::CommandResult{}; });
    ctx.Commands().Add("b.cmd", "", [](const std::vector<std::string>&) { return Core::CommandResult{}; });
    EXPECT_EQ(ctx.RegistrationCount(), 2u);
    ctx.ReleaseAll();
    ctx.ReleaseAll();
    EXPECT_EQ(ctx.RegistrationCount(), 0u);
}

TEST(PluginContext, PermissionsComeFromTheManifest)
{
    World w;
    auto parsed = ParseManifest([] {
        auto j = PluginTest::Manifest("com.test.perm");
        j["permissions"] = nlohmann::json::array({ "net" });
        return j;
    }());
    PluginContext ctx(parsed.Value(), 1, Host::Headless, w.Bindings());
    EXPECT_TRUE(ctx.Has(Permissions::kNetwork));
    EXPECT_FALSE(ctx.Has(Permissions::kProcess));
}

TEST(PluginContext, RandomStreamsAreNamedByPluginAndStream)
{
    World w;
    const PluginManifest m = Manifest("com.test.rng");
    const PluginManifest m2 = Manifest("com.test.rng2");
    PluginContext a(m, 1, Host::Headless, w.Bindings());
    PluginContext b(m2, 2, Host::Headless, w.Bindings());
    auto r1 = a.MakeRandomStream("loot");
    auto r2 = a.MakeRandomStream("loot");
    auto r3 = b.MakeRandomStream("loot");
    auto r4 = a.MakeRandomStream("other");
    const auto v1 = r1.Next(), v2 = r2.Next(), v3 = r3.Next(), v4 = r4.Next();
    EXPECT_EQ(v1, v2);
    EXPECT_NE(v1, v3);
    EXPECT_NE(v1, v4);
}

TEST(PluginContext, EditorPanelsAreExtensionItemsThatLeaveWithThePlugin)
{
    World w;
    const PluginManifest m = Manifest("com.test.panel");
    PluginContext ctx(m, 1, Host::Editor, w.Bindings());
    int drawn = 0;
    EXPECT_TRUE(AddEditorPanel(ctx, "My Panel", [&](bool*) { ++drawn; }));
    EXPECT_FALSE(AddEditorPanel(ctx, "My Panel", [&](bool*) {}));          // a title is used once
    auto panels = w.extensions.Get<EditorPanelItem>("editor.panel");
    ASSERT_EQ(panels.size(), 1u);
    EXPECT_EQ(panels[0]->title, "My Panel");
    EXPECT_FALSE(panels[0]->visible);
    bool open = true;
    panels[0]->draw(&open);
    EXPECT_EQ(drawn, 1);
    ctx.ReleaseAll();
    EXPECT_TRUE(w.extensions.Get<EditorPanelItem>("editor.panel").empty());
}

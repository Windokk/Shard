#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "engine/core/ecs/registry.hpp"
#include "engine/platform/devices/iinput.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/plugins/wasm/wasm_plugin.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/time_manager.hpp"
#include "engine/world/world.hpp"
#include "plugins/asset_test_context.hpp"
#include "plugins/editor_fakes.hpp"

// The game of tests/plugin_fixtures/game_scripts (the one GameScriptsTest loads as a native plugin), compiled to WebAssembly by the SDK's own
// `shard_sdk_add_plugin(... TARGET wasm)` (a wasi-sdk : SHARD_TEST_WASM_TOOLCHAIN) and run in the sandbox against the same expectations : the same
// source, the same behaviour. Only built when a toolchain is given.

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using nlohmann::json;

namespace {
    const char* kId = "shard.test.wasmgame";
#ifdef SHARD_TEST_WASM_GAME_DIR
    const char* kDir = SHARD_TEST_WASM_GAME_DIR;
#else
    const char* kDir = "";
#endif

    class FakeInput : public Core::Platform::IInput {
    public:
        void Init() override {}
        void Tick() override {}
        void Shutdown() override {}
        bool IsKeyDown(Input::Key key) const override { return down.count(key) != 0; }
        bool IsKeyUp(Input::Key key) const override { return down.count(key) == 0; }
        bool WasKeyPressed(Input::Key key) const override { return pressed.count(key) != 0; }
        bool WasKeyReleased(Input::Key) const override { return false; }
        bool IsMouseDown(Input::MouseButton) const override { return false; }
        bool IsMouseUp(Input::MouseButton) const override { return true; }
        bool WasMousePressed(Input::MouseButton) const override { return false; }
        bool WasMouseReleased(Input::MouseButton) const override { return false; }
        void SetCursorVisibility(Core::Platform::CursorVisibility) const override {}
        void GetCursorPos(double* x, double* y) const override { *x = 0.0; *y = 0.0; }
        void SetCursorPos(double, double) const override {}
        std::set<Input::Key> down, pressed;
    };

    class GameContext : public PluginTest::AssetContext {
    public:
        GameContext() { time.Init(1.0f / 60.0f); }
        Time::TimeManager* GetTimeManager() const override { return const_cast<Time::TimeManager*>(&time); }
        Core::Platform::IInput* GetInputManager() const override { return const_cast<FakeInput*>(&input); }
        Time::TimeManager time;
        FakeInput input;
    };

    class WasmGameTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
#ifndef SHARD_TEST_WASM_GAME_DIR
            GTEST_SKIP() << "no wasi-sdk given (SHARD_TEST_WASM_TOOLCHAIN) : the sandboxed game was not built";
#else
            PluginManager::Get().ResetForTests();
            config.host = Host::Headless;
            config.registerConsoleCommands = false;
            config.pluginPaths.push_back(kDir);
            Core::SetEngine(&context);
#endif
        }
        void TearDown() override
        {
            world.reset();
            PluginManager::Get().ResetForTests();
            Core::SetEngine(nullptr);
        }

        PluginManager& pm() { return PluginManager::Get(); }
        PluginRecord* Rec() { return pm().Find(kId); }

        uint64_t Call(const char* name, std::vector<uint64_t> args = {})
        {
            std::vector<uint64_t> results;
            std::string error;
            EXPECT_TRUE(CallWasmExport(Rec()->owner, name, args, results, &error)) << error;
            return results.empty() ? 0 : results[0];
        }
        // game_stat("name") and the like take a pointer : the name is written in the module's scratch buffer
        uint64_t WithName(const char* function, const char* name)
        {
            const uint32_t scratch = uint32_t(Call("game_scratch"));
            WriteWasmMemory(Rec()->owner, scratch, name, uint32_t(std::strlen(name) + 1));
            return Call(function, { scratch });
        }
        int Stat(const char* name) { return int(int32_t(uint32_t(WithName("game_stat", name)))); }
        float Real(const char* name) { const uint32_t bits = uint32_t(WithName("game_float", name)); float f; std::memcpy(&f, &bits, 4); return f; }
        std::string Text(const char* name)
        {
            const uint32_t at = uint32_t(WithName("game_text", name));
            char buffer[64] = {};
            ReadWasmMemory(Rec()->owner, at, buffer, 63);
            return buffer;
        }
        float Dt(const char* function) { const uint32_t bits = uint32_t(Call(function)); float f; std::memcpy(&f, &bits, 4); return f; }

        void Start()
        {
            std::string error;
            ASSERT_TRUE(pm().Startup(config, &error)) << error;
            ASSERT_NE(Rec(), nullptr);
            ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
            pm().OnEngineSystemsReady(&context, &scheduler, &events);
            pm().OnEngineInitialized();
            ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
            world = std::make_unique<Worlds::World>("w", Filesystem::Path("w.world"));
        }

        std::shared_ptr<Objects::Actor> NewActor(const std::string& name, std::shared_ptr<Objects::Actor> parent = nullptr)
        {
            auto actor = Core::Object::CreateWithContext<Objects::Actor>(&context, name, &context);
            if (parent) parent->AddChild(actor);
            else world->AddActor(actor);
            return actor;
        }
        std::string Type(const char* script) const { return std::string(kId) + "." + script; }
        bool Alive(const std::shared_ptr<Objects::Actor>& actor) { return context.GetObjectIDManager()->GetObjectFromID(actor->GetID()).get() == actor.get(); }

        PluginManagerConfig config;
        GameContext context;
        Core::Ecs::Scheduler scheduler;
        Events::EventDispatcher events;
        std::unique_ptr<Worlds::World> world;
    };
}

TEST_F(WasmGameTest, EachFileRegistersItsOwnPiecesInAFixedOrder)
{
    Start();
    auto& registry = Objects::Components::GetComponentRegistry();
    EXPECT_TRUE(registry.IsRegistered(Type("Health")));
    EXPECT_TRUE(registry.IsRegistered(Type("Mover")));
    EXPECT_TRUE(registry.IsRegistered(Type("Spawner")));
    EXPECT_TRUE(registry.IsRegistered(Type("Velocity")));
    EXPECT_TRUE(scheduler.Has(std::string(kId) + "/drift"));
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(WasmGameTest, AScriptRunsLikeTheEnginesOwnAndTheOtherFilesTalkToIt)
{
    Start();
    auto hero = NewActor("Hero");
    ASSERT_NE(hero->AddComponentByName(Type("Health")), nullptr);
    ASSERT_NE(hero->AddComponentByName(Type("Mover")), nullptr);
    EXPECT_EQ(Stat("created"), 1);

    context.time.Advance(0.5f);
    world->Play();
    EXPECT_EQ(Stat("played"), 1);
    auto health = hero->GetComponents()[1];
    EXPECT_EQ(health->Serialize()["label"].get<std::string>(), "unnamed");

    world->Tick();
    EXPECT_EQ(Stat("ticks"), 1);
    EXPECT_FLOAT_EQ(Dt("game_dt"), 0.5f);
    EXPECT_FLOAT_EQ(hero->transform->GetPosition().x, 1.0f);

    context.input.down.insert(Input::Key::D);
    world->Tick();
    EXPECT_FLOAT_EQ(hero->transform->GetPosition().x, 3.0f);
    context.input.down.clear();

    world->FixedTick();
    world->LateTick();
    EXPECT_EQ(Stat("fixedTicks"), 1);
    EXPECT_EQ(Stat("lateTicks"), 1);
    EXPECT_NEAR(Dt("game_fixed_dt"), 1.0f / 60.0f, 1e-6f);

    context.input.pressed.insert(Input::Key::Space);
    world->Tick();
    EXPECT_EQ(Stat("crossScript"), 1);
    EXPECT_FLOAT_EQ(health->Serialize()["current"].get<float>(), 40.0f);
    EXPECT_FALSE(health->Serialize().contains("timesHurt"));

    world->Tick();
    EXPECT_TRUE(Alive(hero));
    world->Tick();
    EXPECT_FALSE(Alive(hero));                                      // Health destroyed its own actor from inside the module's tick
    EXPECT_EQ(Stat("destroyed"), 1);
    world->Tick();
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(WasmGameTest, AScriptCreatesFindsAndSpawnsActors)
{
    Start();
    auto hero = NewActor("Hero");
    auto spawner = NewActor("Spawner");
    auto script = spawner->AddComponentByName(Type("Spawner"));
    ASSERT_NE(script, nullptr);
    script->Deserialize(json::parse(R"({"active":true,"count":3})"));

    world->Play();
    EXPECT_EQ(Stat("minions"), 3);
    EXPECT_EQ(spawner->GetChildrenCount(), 3);
    EXPECT_EQ(Stat("created"), 3);
    EXPECT_EQ(Stat("spawnedFromJson"), 1);
    EXPECT_EQ(Stat("undeclaredTag"), 0);
    EXPECT_EQ(Rec()->errorCount, 0u);
    (void)hero;
}

TEST_F(WasmGameTest, AScriptIsSavedAndCopiedWithItsFields)
{
    Start();
    auto boss = NewActor("Boss");
    auto health = boss->AddComponentByName(Type("Health"));
    ASSERT_NE(health, nullptr);
    health->Deserialize(json::parse(R"({"active":true,"maxHealth":250.0,"current":200.0,"label":"the boss","icon":9})"));
    const ordered_json saved = health->Serialize();
    EXPECT_FLOAT_EQ(saved["maxHealth"].get<float>(), 250.0f);
    EXPECT_EQ(saved["label"].get<std::string>(), "the boss");

    auto copy = boss->Clone();
    ASSERT_NE(copy, nullptr);
    auto copied = copy->GetComponents()[1];
    EXPECT_FLOAT_EQ(copied->Serialize()["maxHealth"].get<float>(), 250.0f);
    EXPECT_FLOAT_EQ(copied->Serialize()["current"].get<float>(), 200.0f);
    EXPECT_EQ(copied->Serialize()["label"].get<std::string>(), "the boss");
    health->Deserialize(json::parse(R"({"active":true,"label":"changed"})"));
    EXPECT_EQ(copied->Serialize()["label"].get<std::string>(), "the boss");
}

TEST_F(WasmGameTest, AnEcsComponentAndItsSystemLiveInTheirOwnFile)
{
    Start();
    Core::Ecs::Registry& ecs = *context.GetEcs();
    struct Velocity { float x, y, z; };
    const auto id = Core::Ecs::ComponentRegistry::Global().Register(("c:" + Type("Velocity")).c_str(), sizeof(Velocity), alignof(Velocity));
    const Core::Ecs::Entity e = ecs.Create();
    const Velocity start{ 0.f, 0.f, 0.f };
    ASSERT_TRUE(ecs.AddRaw(e, id, &start));

    Core::Ecs::Scheduler::FrameTiming timing;
    scheduler.RunFrame(ecs, nullptr, timing);
    scheduler.RunFrame(ecs, nullptr, timing);
    EXPECT_FLOAT_EQ(static_cast<const Velocity*>(ecs.GetRaw(e, id))->x, 2.0f);
    EXPECT_EQ(Stat("driftSteps"), 2);
}

TEST_F(WasmGameTest, ThePlayerOfTheTutorialMovesAndHurtsTheBoss)
{
    Start();
    auto boss = NewActor("Boss");
    auto bossHealth = boss->AddComponentByName(Type("Health"));
    auto hero = NewActor("Hero");
    ASSERT_NE(hero->AddComponentByName(Type("Player")), nullptr);

    context.time.Advance(0.2f);
    world->Play();
    context.input.down.insert(Input::Key::W);
    world->Tick();
    world->Tick();
    EXPECT_NEAR(hero->transform->GetPosition().z, -2.0f, 1e-4f);
    EXPECT_NEAR(hero->transform->GetPosition().x, 0.0f, 1e-4f);
    EXPECT_FLOAT_EQ(bossHealth->Serialize()["current"].get<float>(), 80.0f);
    EXPECT_EQ(Rec()->errorCount, 0u);
}


// ---- the same, from a module in the sandbox : shard.editor 1.1 : menus, toolbar, inspectors, gizmos, selection, notifications, richer widgets ----

TEST_F(WasmGameTest, AnEditorPluginContributesMenusToolbarInspectorsGizmosAndFollowsTheSelection)
{
    EditorFakes::Log().clear();
    EditorFakes::Notified().clear();
    SetEditorUiBackend(EditorFakes::Backend());
    Start();
    auto& manager = pm();

    // A menu item and a toolbar button
    const auto menus = manager.Extensions().Get<EditorMenuItem>("editor.menu");
    ASSERT_EQ(menus.size(), 1u);
    EXPECT_EQ(menus[0]->path, "Tools/Demo/Bake all");
    menus[0]->run();
    EXPECT_EQ(Stat("menuRuns"), 1);
    const auto toolbar = manager.Extensions().Get<EditorToolbarItem>("editor.toolbar");
    ASSERT_EQ(toolbar.size(), 1u);
    toolbar[0]->run();
    EXPECT_EQ(Stat("toolbarRuns"), 1);
    ASSERT_EQ(EditorFakes::Notified().size(), 1u);
    EXPECT_EQ(EditorFakes::Notified()[0].first, uint32_t(SHARD_EDITOR_NOTIFY_WARNING));
    EXPECT_EQ(EditorFakes::Notified()[0].second, "pinged");

    // An inspector and a gizmo are called with the handle of the actor
    auto hero = NewActor("Hero");
    const uint64_t handle = EditorActorHandle(hero);
    const auto inspectors = manager.Extensions().Get<EditorInspectorItem>("editor.inspector");
    ASSERT_EQ(inspectors.size(), 1u);
    inspectors[0]->draw(EditorFakes::Widgets(), handle);
    EXPECT_EQ(Stat("inspectorDraws"), 1);
    EXPECT_EQ(Text("inspected"), "Hero");
    EXPECT_GE(EditorFakes::Count("separator_text"), 1);
    EXPECT_GE(EditorFakes::Count("begin_disabled"), 1);
    EXPECT_GE(EditorFakes::Count("drag_float3"), 1);
    const auto gizmos = manager.Extensions().Get<EditorGizmoItem>("editor.gizmo");
    ASSERT_EQ(gizmos.size(), 1u);
    gizmos[0]->draw(EditorFakes::Gizmos(), handle);
    EXPECT_EQ(Stat("gizmoDraws"), 1);
    EXPECT_EQ(Text("gizmoActor"), "Hero");
    EXPECT_EQ(EditorFakes::Count("gizmo.line"), 1);
    EXPECT_EQ(EditorFakes::Count("gizmo.sphere"), 1);
    EXPECT_EQ(EditorFakes::Count("gizmo.text"), 1);

    // The selection: told to the plugin, and asked by it
    NotifyEditorSelection({ hero });
    EXPECT_EQ(Stat("selectionEvents"), 1);
    EXPECT_EQ(Stat("selectionSize"), 1);
    NotifyEditorSelection({});
    EXPECT_EQ(Stat("selectionSize"), 0);

    // A window that uses the richer widgets
    const auto panels = manager.Extensions().Get<EditorPanelItem>("editor.panel");
    std::shared_ptr<EditorPanelItem> widgets;
    for (const auto& panel : panels) if (panel->title == "Demo widgets") widgets = panel;
    ASSERT_NE(widgets, nullptr);
    bool open = true;
    widgets->draw(&open);
    EXPECT_EQ(Stat("widgetDraws"), 1);
    EXPECT_EQ(Stat("tabItems"), 1);
    EXPECT_EQ(Stat("tableCells"), 2);
    EXPECT_EQ(Stat("pickedIndex"), 2);                              // the combo's answer went back into the plugin's variable
    EXPECT_EQ(Stat("menuRuns"), 2);                                 // the menu item of its context popup
    EXPECT_FLOAT_EQ(Real("contentWidth"), 123.0f);
    EXPECT_EQ(EditorFakes::Count("begin_tab_bar"), 1);
    EXPECT_EQ(EditorFakes::Count("end_table"), 1);

    // All of it goes with the plugin
    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_TRUE(manager.Extensions().Get<EditorMenuItem>("editor.menu").empty());
    EXPECT_TRUE(manager.Extensions().Get<EditorToolbarItem>("editor.toolbar").empty());
    EXPECT_TRUE(manager.Extensions().Get<EditorInspectorItem>("editor.inspector").empty());
    EXPECT_TRUE(manager.Extensions().Get<EditorGizmoItem>("editor.gizmo").empty());
    NotifyEditorSelection({});                                      // nobody is called any more
    SetEditorUiBackend(nullptr);
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(WasmGameTest, AReferenceToAnotherActorIsSavedByPathAndFoundAgain)
{
    Start();
    auto level = NewActor("Level");
    auto turretActor = NewActor("Turret");
    auto script = turretActor->AddComponentByName(Type("Turret"));
    ASSERT_NE(script, nullptr);
    script->Deserialize(json::parse(R"({"active":true,"target":"Level/Enemy","note":"aim","power":2.0})"));
    EXPECT_EQ(script->Serialize()["target"].get<std::string>(), "Level/Enemy");
    world->Play();
    context.time.Advance(0.1f);
    world->Tick();
    EXPECT_EQ(Stat("aimed"), 0);

    auto enemy = NewActor("Enemy", level);
    enemy->transform->SetPosition(glm::vec3(0.0f, 0.0f, -4.0f));
    world->Tick();
    EXPECT_EQ(Stat("aimed"), 1);
    EXPECT_NEAR(Real("targetDistance"), 4.0f, 1e-4f);

    // The plugin's own std::string, inside the module, through text_get and text_set
    const ordered_json saved = script->Serialize();
    EXPECT_EQ(saved["note"].get<std::string>(), "aim");
    EXPECT_EQ(saved["target"].get<std::string>(), "Level/Enemy");
    EXPECT_FLOAT_EQ(saved["power"].get<float>(), 2.0f);
    script->Deserialize(json::parse(R"({"active":true,"note":"fire"})"));
    world->Tick();
    const uint32_t text = uint32_t(WithName("game_text", "lastNote"));
    char buffer[16] = {};
    ASSERT_TRUE(ReadWasmMemory(Rec()->owner, text, buffer, 5));
    EXPECT_STREQ(buffer, "fire");
    EXPECT_EQ(Rec()->errorCount, 0u);
}

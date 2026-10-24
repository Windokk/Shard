#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/audio/audio_manager.hpp"
#include "engine/audio/audio_world_data.hpp"
#include "engine/audio/audioID.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/physics/physics_world_data.hpp"
#include "engine/renderer/components/render_world_data.hpp"
#include "engine/core/ecs/registry.hpp"
#include "engine/platform/devices/iinput.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/components/registry/component_registry.hpp"
#include "engine/world/time_manager.hpp"
#include "engine/world/world.hpp"
#include "plugins/asset_test_context.hpp"
#include "plugins/editor_fakes.hpp"
#include <shard/shard.h>

// The gameplay layer of the SDK (shard/game.hpp) end to end: tests/plugin_fixtures/game_scripts is a game of several source files (scripts, a
// component and a system, the one-line module), loaded through the C ABI and driven by a world the way the engine drives its own scripts.

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using nlohmann::json;

namespace {
    const char* kId = "shard.test.gamescripts";

    // The keyboard of a test: what is down, and what was pressed this frame
    class FakeInput : public Core::Platform::IInput {
    public:
        void Init() override {}
        void Tick() override {}
        void Shutdown() override {}
        bool IsKeyDown(Input::Key key) const override { return down.count(key) != 0; }
        bool IsKeyUp(Input::Key key) const override { return down.count(key) == 0; }
        bool WasKeyPressed(Input::Key key) const override { return pressed.count(key) != 0; }
        bool WasKeyReleased(Input::Key) const override { return false; }
        bool IsMouseDown(Input::MouseButton button) const override { return mouseDown.count(button) != 0; }
        bool IsMouseUp(Input::MouseButton button) const override { return mouseDown.count(button) == 0; }
        bool WasMousePressed(Input::MouseButton button) const override { return mousePressed.count(button) != 0; }
        bool WasMouseReleased(Input::MouseButton) const override { return false; }
        void SetCursorVisibility(Core::Platform::CursorVisibility visibility) const override { cursorMode = visibility; }
        void GetCursorPos(double* x, double* y) const override { *x = 12.0; *y = 34.0; }
        void SetCursorPos(double, double) const override {}

        std::set<Input::Key> down, pressed;
        std::set<Input::MouseButton> mouseDown, mousePressed;
        mutable Core::Platform::CursorVisibility cursorMode = Core::Platform::CursorVisibility::Visible;
    };

    class GameContext : public PluginTest::AssetContext {
    public:
        GameContext()
        {
            time.Init(1.0f / 60.0f);
        }
        /// Needs the engine set (the sound kind is registered with its resources) : called once the test has made this context the engine
        void StartAudio() { audio.Init(1.0f, true); }     // no sound device : every call is accepted, nothing plays
        Time::TimeManager* GetTimeManager() const override { return const_cast<Time::TimeManager*>(&time); }
        Audio::AudioManager* GetAudioManager() const override { return const_cast<Audio::AudioManager*>(&audio); }
        Audio::AudioIDManager* GetAudioIDManager() const override { return const_cast<Audio::AudioIDManager*>(&audioIds); }
        Audio::AudioManager audio;
        Audio::AudioIDManager audioIds;
        Core::Platform::IInput* GetInputManager() const override { return const_cast<FakeInput*>(&input); }
        Rendering::IRenderContext* GetRenderContext() const override { return nullptr; }           // a headless engine : nothing is drawn
        Time::TimeManager time;
        FakeInput input;
    };

    class GameScriptsTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            PluginManager::Get().ResetForTests();
            config.host = Host::Headless;
            config.registerConsoleCommands = false;
            config.pluginPaths.push_back(std::string(SHARD_TEST_PLUGIN_DIR) + "/" + kId);
            Core::SetEngine(&context);
            context.StartAudio();
        }
        void TearDown() override
        {
            world.reset();
            SetEditorUiBackend(nullptr);
            PluginManager::Get().ResetForTests();
            Core::SetEngine(nullptr);
        }

        PluginManager& pm() { return PluginManager::Get(); }
        PluginRecord* Rec() { return pm().Find(kId); }
        template <typename Fn>
        Fn Export(const char* name) { return Rec()->library.Symbol<Fn>(name); }
        int Stat(const char* name) { return Export<int (*)(const char*)>("game_stat")(name); }

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

        bool Alive(const std::shared_ptr<Objects::Actor>& actor)
        {
            return context.GetObjectIDManager()->GetObjectFromID(actor->GetID()).get() == actor.get();
        }

        PluginManagerConfig config;
        GameContext context;
        Core::Ecs::Scheduler scheduler;
        Events::EventDispatcher events;
        std::unique_ptr<Worlds::World> world;
    };
}

TEST_F(GameScriptsTest, EachFileRegistersItsOwnPiecesInAFixedOrder)
{
    Start();
    auto& registry = Objects::Components::GetComponentRegistry();
    EXPECT_TRUE(registry.IsRegistered(Type("Health")));
    EXPECT_TRUE(registry.IsRegistered(Type("Mover")));
    EXPECT_TRUE(registry.IsRegistered(Type("Spawner")));
    EXPECT_TRUE(registry.IsRegistered(Type("Velocity")));           // an ECS component, defined in yet another file
    EXPECT_TRUE(scheduler.Has(std::string(kId) + "/drift"));        // its system, registered after the components whatever the link order
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(GameScriptsTest, AScriptRunsLikeTheEnginesOwnAndTheOtherFilesTalkToIt)
{
    Start();
    auto hero = NewActor("Hero");
    ASSERT_NE(hero->AddComponentByName(Type("Health")), nullptr);
    ASSERT_NE(hero->AddComponentByName(Type("Mover")), nullptr);
    EXPECT_EQ(Stat("created"), 1);                                  // OnCreate, as the component joined an actor that is in a world

    context.time.Advance(0.5f);
    world->Play();
    EXPECT_EQ(Stat("played"), 1);
    auto health = hero->GetComponents()[1];                         // 0 is the Transform
    EXPECT_EQ(health->Serialize()["label"].get<std::string>(), "unnamed");     // the script set its text in OnPlay

    world->Tick();
    EXPECT_EQ(Stat("ticks"), 1);
    EXPECT_FLOAT_EQ(Export<float (*)()>("game_dt")(), 0.5f);        // the frame's delta time, handed to OnTick
    EXPECT_FLOAT_EQ(hero->transform->GetPosition().x, 1.0f);        // Mover: speed 2 * 0.5 s along x

    // The keyboard
    context.input.down.insert(Input::Key::D);
    world->Tick();
    EXPECT_FLOAT_EQ(hero->transform->GetPosition().x, 3.0f);        // + 2 * 0.5 * (1 + 1)
    context.input.down.clear();

    world->FixedTick();
    world->LateTick();
    EXPECT_EQ(Stat("fixedTicks"), 1);
    EXPECT_EQ(Stat("lateTicks"), 1);
    EXPECT_NEAR(Export<float (*)()>("game_fixed_dt")(), 1.0f / 60.0f, 1e-6f);

    // Space: the Mover hurts the Health of the same actor
    context.input.pressed.insert(Input::Key::Space);
    world->Tick();
    EXPECT_EQ(Stat("crossScript"), 1);
    EXPECT_FLOAT_EQ(health->Serialize()["current"].get<float>(), 40.0f);
    EXPECT_FALSE(health->Serialize().contains("timesHurt"));        // a runtime field: shown, not saved

    // Hurt to death: Health destroys its own actor in the middle of the world's tick, and the rest of the tick goes on
    world->Tick();                                                  // Space still pressed: 40 - 60
    EXPECT_TRUE(Alive(hero));
    world->Tick();
    EXPECT_FALSE(Alive(hero));
    EXPECT_EQ(Stat("destroyed"), 1);
    world->Tick();                                                  // nothing left to call
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(GameScriptsTest, AScriptCreatesFindsAndSpawnsActors)
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
    EXPECT_EQ(Stat("created"), 3);                                  // each minion got a Health script, OnCreate ran for each
    EXPECT_STREQ(Export<const char* (*)()>("game_found_name")(), "Hero");
    EXPECT_EQ(Stat("spawnedFromJson"), 1);
    EXPECT_EQ(Stat("undeclaredTag"), 0);                            // a tag nobody declared is refused, not invented
    EXPECT_EQ(Rec()->errorCount, 0u);
    (void)hero;
}

TEST_F(GameScriptsTest, AScriptIsSavedAndCopiedWithItsFields)
{
    Start();
    auto boss = NewActor("Boss");
    auto health = boss->AddComponentByName(Type("Health"));
    ASSERT_NE(health, nullptr);
    health->Deserialize(json::parse(R"({"active":true,"maxHealth":250.0,"current":200.0,"label":"the boss","icon":9})"));

    const ordered_json saved = health->Serialize();
    EXPECT_EQ(saved["type"], Type("Health"));
    EXPECT_FLOAT_EQ(saved["maxHealth"].get<float>(), 250.0f);
    EXPECT_EQ(saved["label"].get<std::string>(), "the boss");

    // A copy of the actor (play mode) builds a new object and carries the fields into it
    auto copy = boss->Clone();
    ASSERT_NE(copy, nullptr);
    auto copied = copy->GetComponents()[1];
    const ordered_json copySaved = copied->Serialize();
    EXPECT_FLOAT_EQ(copySaved["maxHealth"].get<float>(), 250.0f);
    EXPECT_FLOAT_EQ(copySaved["current"].get<float>(), 200.0f);
    EXPECT_EQ(copySaved["label"].get<std::string>(), "the boss");
    health->Deserialize(json::parse(R"({"active":true,"label":"changed"})"));
    EXPECT_EQ(copied->Serialize()["label"].get<std::string>(), "the boss");   // its own text, not the same one

    // The descriptor the inspector reads: the listed fields, with the text as a text and the asset as an asset
    const ClassDescriptor* descriptor = health->GetDescriptor();
    ASSERT_NE(descriptor, nullptr);
    bool sawText = false, sawAsset = false;
    for (const FieldInfo* field : descriptor->fields) {
        if (std::string(field->name) == "label") sawText = field->type == TypeID::String;
        if (std::string(field->name) == "icon") sawAsset = field->type == TypeID::Asset;
    }
    EXPECT_TRUE(sawText);
    EXPECT_TRUE(sawAsset);
}

TEST_F(GameScriptsTest, AnEcsComponentAndItsSystemLiveInTheirOwnFile)
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

TEST_F(GameScriptsTest, ScriptsStopBeingCalledWhenThePluginGoes)
{
    Start();
    auto hero = NewActor("Hero");
    ASSERT_NE(hero->AddComponentByName(Type("Health")), nullptr);
    context.time.Advance(0.1f);
    world->Tick();
    EXPECT_EQ(Stat("ticks"), 1);
    auto stats = Export<int (*)(const char*)>("game_stat");

    // Its types are gone from the registry with the plugin: a script already in a world must not be called into a library that is leaving
    // (the manager refuses to disable a plugin whose components a world holds, so this goes through the release the manager would run)
    CAbi::ReleaseScripts(Rec()->owner);
    world->Tick();
    EXPECT_EQ(stats("ticks"), 1);
}

TEST_F(GameScriptsTest, ThePlayerOfTheTutorialMovesAndHurtsTheBoss)
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
    EXPECT_NEAR(hero->transform->GetPosition().z, -2.0f, 1e-4f);        // forward is -Z : 5 units/s * 0.2 s, twice
    EXPECT_NEAR(hero->transform->GetPosition().x, 0.0f, 1e-4f);
    EXPECT_FLOAT_EQ(bossHealth->Serialize()["current"].get<float>(), 80.0f);     // found by name, a script of another file, hurt each tick
    EXPECT_EQ(Rec()->errorCount, 0u);
}

// ---- ABI 1.5 ----

TEST_F(GameScriptsTest, AReferenceToAnotherActorIsSavedByPathAndFoundAgain)
{
    Start();
    auto level = NewActor("Level");
    auto turretActor = NewActor("Turret");
    auto script = turretActor->AddComponentByName(Type("Turret"));
    ASSERT_NE(script, nullptr);

    // The path is known before the actor is : the reference waits, and is found when the script runs
    script->Deserialize(json::parse(R"({"active":true,"target":"Level/Enemy","note":"aim","power":2.0})"));
    EXPECT_EQ(script->Serialize()["target"].get<std::string>(), "Level/Enemy");   // not found yet : the path stays
    world->Play();
    context.time.Advance(0.1f);
    world->Tick();
    EXPECT_EQ(Stat("aimed"), 0);

    auto enemy = NewActor("Enemy", level);
    enemy->transform->SetPosition(glm::vec3(0.0f, 0.0f, -4.0f));
    world->Tick();
    EXPECT_EQ(Stat("aimed"), 1);
    EXPECT_NEAR(Export<float (*)(const char*)>("game_float")("targetDistance"), 4.0f, 1e-4f);

    // The plugin's own std::string went through its callbacks, both ways
    EXPECT_STREQ(Export<const char* (*)(const char*)>("game_text")("lastNote"), "aim");
    const ordered_json saved = script->Serialize();
    EXPECT_EQ(saved["note"].get<std::string>(), "aim");
    EXPECT_EQ(saved["target"].get<std::string>(), "Level/Enemy");
    EXPECT_FLOAT_EQ(saved["power"].get<float>(), 2.0f);
    script->Deserialize(json::parse(R"({"active":true,"note":"fire"})"));
    world->Tick();
    EXPECT_STREQ(Export<const char* (*)(const char*)>("game_text")("lastNote"), "fire");

    // A copy of the actor (play mode) keeps the path and the text, and finds the actor once it is in the world
    world->SetLoaded(true);                                         // the engine adds the scripts of a copy to a loaded world only
    auto copy = turretActor->Clone();
    ASSERT_NE(copy, nullptr);
    auto copied = copy->GetComponents()[1];
    EXPECT_EQ(copied->Serialize()["note"].get<std::string>(), "fire");
    EXPECT_EQ(copied->Serialize()["target"].get<std::string>(), "Level/Enemy");
    world->Tick();
    EXPECT_EQ(Stat("aimed"), 4);                                    // the original, the copy and the original again

    // The actor it pointed at is gone : the reference is empty, not dangling
    enemy->Destroy();
    EXPECT_EQ(script->Serialize()["target"].get<std::string>(), "");
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(GameScriptsTest, AScriptCallsTheWorldAudioAndPrefabs)
{
    Start();
    auto actor = NewActor("Host");
    ASSERT_NE(actor->AddComponentByName(Type("Misc")), nullptr);
    world->Play();
    EXPECT_EQ(Stat("worldLoad"), 0);                                // this test engine has no world manager : nothing to load
    EXPECT_EQ(Stat("playAudio"), 0);                                // the actor has no audio component
    EXPECT_EQ(Stat("oneShot"), SHARD_E_NOT_FOUND);                  // the project has no such clip
    EXPECT_EQ(Stat("prefab"), 1);                                   // crate.json of the plugin's content : an actor with a child
    EXPECT_EQ(Stat("missingPrefab"), 1);

    // A stream of the plugin is the engine's own generator in the plugin's hands : the same numbers, bit for bit
    EXPECT_EQ(Stat("randomSame"), 1);
    EXPECT_EQ(Stat("randomOther"), 1);
    Core::Random dice = context.MakeRandomStream(std::string(kId) + "/dice");
    EXPECT_EQ(uint32_t(Stat("randomFirst")), dice.Next());
    Core::Random sphere = context.MakeRandomStream(std::string(kId) + "/sphere");
    const glm::vec3 expected = sphere.OnUnitSphere();
    auto real = Export<float (*)(const char*)>("game_float");
    EXPECT_EQ(real("sphereX"), expected.x);
    EXPECT_EQ(real("sphereY"), expected.y);
    EXPECT_EQ(real("sphereZ"), expected.z);
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(GameScriptsTest, APanelIsOnlyAddedWhereThereIsAnEditor)
{
    Start();
    EXPECT_TRUE(pm().Extensions().Get<EditorPanelItem>("editor.panel").empty());     // no editor : the plugin loaded without it
}

namespace {
    // The widgets of a test editor : what the panel draws is recorded
    std::vector<std::string> gDrawn;
    void SHARD_CALL UiText(ShardStr t) { gDrawn.push_back("text:" + std::string(t.data, size_t(t.size))); }
    int32_t SHARD_CALL UiSliderInt(ShardStr, int32_t* value, int32_t, int32_t) { gDrawn.push_back("slider:" + std::to_string(*value)); *value = 7; return SHARD_TRUE; }
    int32_t SHARD_CALL UiInputText(ShardStr, char* buffer, uint64_t) { gDrawn.push_back(std::string("input:") + buffer); std::snprintf(buffer, 8, "alice"); return SHARD_TRUE; }
    int32_t SHARD_CALL UiButton(ShardStr) { gDrawn.push_back("button"); return SHARD_TRUE; }
    bool UiBegin(const char* title, bool*) { gDrawn.push_back(std::string("begin:") + title); return true; }
    void UiEnd() { gDrawn.push_back("end"); }
}

TEST_F(GameScriptsTest, APanelDrawsWithTheWidgetsOfTheEditor)
{
    ShardUiApi widgets{};
    widgets.structSize = sizeof(widgets);
    widgets.text = &UiText;
    widgets.slider_int = &UiSliderInt;
    widgets.input_text = &UiInputText;
    widgets.button = &UiButton;
    EditorUiBackend backend;
    backend.widgets = &widgets;
    backend.begin = &UiBegin;
    backend.end = &UiEnd;
    gDrawn.clear();
    SetEditorUiBackend(&backend);
    Start();

    const auto all = pm().Extensions().Get<EditorPanelItem>("editor.panel");
    ASSERT_EQ(all.size(), 2u);                                      // this one, and the window of the richer widgets
    std::vector<std::shared_ptr<EditorPanelItem>> panels;
    for (const auto& panel : all) if (panel->title == "Game debug") panels.push_back(panel);
    ASSERT_EQ(panels.size(), 1u);
    bool open = true;
    panels[0]->draw(&open);
    EXPECT_EQ(Stat("panelDraws"), 1);
    EXPECT_EQ(Stat("panelClicks"), 1);                              // the button said it was pressed
    const std::vector<std::string> expected = { "begin:Game debug", "text:hello", "slider:3", "input:bob", "button", "end" };
    EXPECT_EQ(gDrawn, expected);
    panels[0]->draw(&open);
    EXPECT_EQ(gDrawn[8], "slider:7");                               // a value the widget changed is kept by the panel

    // It goes with the plugin
    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_TRUE(pm().Extensions().Get<EditorPanelItem>("editor.panel").empty());
    SetEditorUiBackend(nullptr);
}

// ---- shard.editor 1.1 : menus, toolbar, inspectors, gizmos, selection, notifications, richer widgets ----

TEST_F(GameScriptsTest, AnEditorPluginContributesMenusToolbarInspectorsGizmosAndFollowsTheSelection)
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
    EXPECT_STREQ(Export<const char* (*)(const char*)>("game_text")("inspected"), "Hero");
    EXPECT_GE(EditorFakes::Count("separator_text"), 1);
    EXPECT_GE(EditorFakes::Count("begin_disabled"), 1);
    EXPECT_GE(EditorFakes::Count("drag_float3"), 1);
    const auto gizmos = manager.Extensions().Get<EditorGizmoItem>("editor.gizmo");
    ASSERT_EQ(gizmos.size(), 1u);
    gizmos[0]->draw(EditorFakes::Gizmos(), handle);
    EXPECT_EQ(Stat("gizmoDraws"), 1);
    EXPECT_STREQ(Export<const char* (*)(const char*)>("game_text")("gizmoActor"), "Hero");
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
    EXPECT_FLOAT_EQ(Export<float (*)(const char*)>("game_float")("contentWidth"), 123.0f);
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

namespace {
    // Physics on a real physics manager, no window and no GPU
    class GamePhysicsTest : public GameScriptsTest {
    protected:
        void SetUp() override
        {
            GameScriptsTest::SetUp();
            physics.Init(glm::vec3(0.0f, -9.81f, 0.0f));
            context.physicsManager = &physics;
        }
        void TearDown() override
        {
            actors.clear();
            world.reset();
            PluginManager::Get().ResetForTests();       // the plugins' colliders leave the physics world first
            physics.Shutdown();
            context.physicsManager = nullptr;
            GameScriptsTest::TearDown();
        }

        std::shared_ptr<Objects::Actor> Body(const char* name, glm::vec3 position, glm::vec3 scale, JPH::EMotionType motion)
        {
            auto actor = NewActor(name);
            actor->transform->SetPosition(position);
            actor->transform->SetScale(scale);
            actor->AddComponent<Objects::Components::PhysicsBody>()->CreateBody(motion);
            actors.push_back(actor);
            return actor;
        }

        Physics::PhysicsManager physics;
        std::vector<std::shared_ptr<Objects::Actor>> actors;
    };
}

TEST_F(GamePhysicsTest, AScriptPushesBodiesCastsRaysAndHearsContacts)
{
    Start();
    auto ground = Body("Ground", glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f), JPH::EMotionType::Static);
    auto ball = Body("Ball", glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    ASSERT_NE(ball->AddComponentByName(Type("Ball")), nullptr);
    auto probe = NewActor("Probe");
    probe->transform->SetPosition(glm::vec3(10.0f, 5.0f, 10.0f));
    ASSERT_NE(probe->AddComponentByName(Type("Probe")), nullptr);

    // A ray down from the probe meets the ground's top face (the probe has no body of its own to be ignored)
    world->Play();
    EXPECT_EQ(Stat("rayHits"), 1);
    EXPECT_NEAR(Export<float (*)(const char*)>("game_float")("rayDistance"), 5.0f, 0.05f);
    EXPECT_NEAR(Export<float (*)(const char*)>("game_float")("rayNormalY"), 1.0f, 0.01f);
    EXPECT_STREQ(Export<const char* (*)(const char*)>("game_text")("rayActor"), "Ground");

    // The ball lands : its script is told, with the normal pointing from the ball down to the ground
    for (int step = 0; step < 90; ++step) physics.StepSimulation(1.0f / 60.0f, false);
    EXPECT_GE(Stat("contactsAdded"), 1);
    EXPECT_NEAR(Export<float (*)(const char*)>("game_float")("contactNormalY"), -1.0f, 0.05f);
    EXPECT_GE(Export<float (*)(const char*)>("game_float")("contactPenetration"), 0.0f);
    EXPECT_STREQ(Export<const char* (*)(const char*)>("game_text")("contactOther"), "Ground");

    // The fixed tick of the script pushes its body (here, with a zero force : what counts is that the body was found)
    world->FixedTick();
    EXPECT_EQ(Stat("forced"), 1);

    // The same functions from C : velocities, impulses, gravity, and the refusals
    const ShardHostApi* host = GetCHostApi();
    const void* table = nullptr;
    ASSERT_EQ(host->query_interface(ShardStr{ SHARD_IFACE_PHYSICS, std::strlen(SHARD_IFACE_PHYSICS) }, 1, 0, &table), SHARD_OK);
    const auto* api = static_cast<const ShardPhysicsApi*>(table);
    const ShardActor ballHandle = CAbi::ActorHandle(ball);
    const float velocity[3] = { 1.0f, 2.0f, 3.0f };
    float read[3] = {};
    ASSERT_EQ(api->set_velocity(ballHandle, velocity), SHARD_OK);
    ASSERT_EQ(api->get_velocity(ballHandle, read), SHARD_OK);
    EXPECT_NEAR(read[0], 1.0f, 1e-4f);
    EXPECT_NEAR(read[2], 3.0f, 1e-4f);
    const float impulse[3] = { 5000.0f, 0.0f, 0.0f };           // the ball weighs a ton
    ASSERT_EQ(api->add_impulse(ballHandle, impulse), SHARD_OK);
    ASSERT_EQ(api->get_velocity(ballHandle, read), SHARD_OK);
    EXPECT_GT(read[0], 1.5f);                                       // an impulse changes the velocity at once
    ASSERT_EQ(api->get_gravity(ballHandle, read), SHARD_OK);
    EXPECT_NEAR(read[1], -9.81f, 1e-3f);
    EXPECT_EQ(api->get_velocity(CAbi::ActorHandle(probe), read), SHARD_E_NOT_FOUND);     // no body
    const float nonsense[3] = { NAN, 0.0f, 0.0f };
    EXPECT_EQ(api->set_velocity(ballHandle, nonsense), SHARD_E_INVALID_ARG);

    // A ray that goes through the actor that fires it, and one that has nothing to meet
    ShardRayHit hit{};
    hit.structSize = sizeof(hit);
    const float above[3] = { 0.0f, 10.0f, 0.0f };
    const float down[3] = { 0.0f, -1.0f, 0.0f };
    ASSERT_EQ(api->raycast(above, down, 100.0f, SHARD_LAYER_ALL, 0, &hit), SHARD_OK);
    EXPECT_EQ(hit.actor, ballHandle);                               // the ball is in the way
    ASSERT_EQ(api->raycast(above, down, 100.0f, SHARD_LAYER_ALL, ballHandle, &hit), SHARD_OK);
    EXPECT_EQ(hit.actor, CAbi::ActorHandle(ground));                // ignored : the ray goes through it
    EXPECT_EQ(api->raycast(above, down, 1.0f, SHARD_LAYER_ALL, 0, &hit), SHARD_E_NOT_FOUND);
    EXPECT_EQ(api->raycast(above, down, 100.0f, SHARD_LAYER_STATIC, ballHandle, &hit), SHARD_OK);   // only what never moves
    const float nowhere[3] = { 0.0f, 0.0f, 0.0f };
    EXPECT_EQ(api->raycast(above, nowhere, 100.0f, SHARD_LAYER_ALL, 0, &hit), SHARD_E_INVALID_ARG);
    EXPECT_EQ(Rec()->errorCount, 0u);
}

namespace {
    // The game of the example project (src/apps/game) : written with the SDK alone, loaded like any plugin
    class ExampleGameTest : public GamePhysicsTest {
    protected:
        void StartGame()
        {
            Rendering::RegisterRenderingModule();                   // the bullet has a model and a body
            Physics::RegisterPhysicsModule();
            config.pluginPaths.clear();
            config.pluginPaths.push_back(SHARD_GAME_PLUGIN_DIR);
            std::string error;
            ASSERT_TRUE(pm().Startup(config, &error)) << error;
            PluginRecord* game = pm().Find("game");
            ASSERT_NE(game, nullptr);
            ASSERT_EQ(game->state, PluginState::Initialized) << game->error;
            pm().OnEngineSystemsReady(&context, &scheduler, &events);
            pm().OnEngineInitialized();
            ASSERT_EQ(game->state, PluginState::Enabled) << game->error;
            world = std::make_unique<Worlds::World>("w", Filesystem::Path("w.world"));
        }
    };
}

TEST_F(ExampleGameTest, TheCharacterWalksWhereItLooks)
{
    StartGame();
    auto player = NewActor("Player");
    auto character = player->AddComponentByName("game.Character");
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(character->Serialize()["speed"].get<float>(), 30.0f);
    world->Play();
    EXPECT_EQ(context.input.cursorMode, Core::Platform::CursorVisibility::Disabled);       // play mode takes the cursor

    context.input.down.insert(Input::Key::W);
    context.time.Advance(0.1f);
    world->Tick();
    EXPECT_NEAR(player->transform->GetPosition().z, -3.0f, 1e-4f);          // 30 units a second for a tenth of a second, down -Z
    context.input.down.clear();
    context.input.down.insert(Input::Key::D);
    world->Tick();
    EXPECT_NEAR(player->transform->GetPosition().x, 3.0f, 1e-4f);          // to the right
    EXPECT_EQ(pm().Find("game")->errorCount, 0u);
}

TEST_F(ExampleGameTest, TheGunFiresABulletThatFlies)
{
    StartGame();
    auto holder = NewActor("Gun");
    auto gun = holder->AddComponentByName("game.Gun");
    ASSERT_NE(gun, nullptr);
    gun->Deserialize(json::parse(R"({"active":true,"gunShotIntensity":20.0,"bulletLifetime":1.0})"));
    world->Play();

    context.input.mousePressed.insert(Input::MouseButton::Left);
    context.time.Advance(0.1f);
    world->Tick();
    context.input.mousePressed.clear();

    std::shared_ptr<Objects::Actor> bullet;
    for (auto& [id, actor] : world->GetRootActors()) if (actor->GetName() == "Bullet") bullet = actor;
    ASSERT_NE(bullet, nullptr);
    auto body = bullet->GetComponent<Objects::Components::PhysicsBody>();
    ASSERT_NE(body, nullptr);
    EXPECT_FALSE(body->GetBodyID().IsInvalid());
    const glm::vec3 velocity = body->GetLinearVelocity();
    EXPECT_NEAR(velocity.z, -20.0f, 1e-3f);                                // along the gun's forward, at the intensity
    EXPECT_NEAR(bullet->transform->GetPosition().z, -1.0f, 1e-4f);         // a unit in front of the gun

    // After its lifetime the bullet goes
    for (int i = 0; i < 12; ++i) { context.time.Advance(0.1f); world->Tick(); }
    EXPECT_FALSE(Alive(bullet));
    EXPECT_EQ(pm().Find("game")->errorCount, 0u);
}

TEST_F(ExampleGameTest, TheKickerKicksTheSameWayInEveryRun)
{
    StartGame();
    auto ball = Body("Ball", glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
    auto kicker = ball->AddComponentByName("game.RandomKicker");
    ASSERT_NE(kicker, nullptr);
    kicker->Deserialize(json::parse(R"({"active":true,"interval":1,"strength":1000.0})"));
    world->Play();
    auto body = ball->GetComponent<Objects::Components::PhysicsBody>();
    ASSERT_NE(body, nullptr);

    world->Tick();
    const glm::vec3 first = body->GetLinearVelocity();
    EXPECT_GT(glm::length(first), 0.1f);

    // The same stream from the engine's side : the direction of the first kick
    Core::Random expected = context.MakeRandomStream("game/RandomKicker/Ball");
    const glm::vec3 direction = expected.OnUnitSphere();
    EXPECT_GT(glm::dot(glm::normalize(first), direction), 0.999f);
    EXPECT_EQ(pm().Find("game")->errorCount, 0u);
}

TEST_F(GameScriptsTest, FieldsAreDeclaredInTheClassWithTheirOwnOptions)
{
    Start();
    auto actor = NewActor("Subject");
    auto declared = actor->AddComponentByName(Type("Declared"));
    ASSERT_NE(declared, nullptr);

    // The inspector's view : the members declared with SHARD_FIELD, in the order of the class, each with the options it was given
    const ClassDescriptor* descriptor = declared->GetDescriptor();
    ASSERT_NE(descriptor, nullptr);
    std::vector<std::string> names;
    const FieldInfo* power = nullptr;
    const FieldInfo* ammo = nullptr;
    for (const FieldInfo* field : descriptor->fields) {
        names.push_back(field->name);
        if (names.back() == "power") power = field;
        if (names.back() == "ammo") ammo = field;
    }
    EXPECT_EQ(names, (std::vector<std::string>{ "power", "ammo", "aim", "label" }));       // not "secret" (Hidden), not "plain" (no SHARD_FIELD)
    ASSERT_NE(power, nullptr);
    EXPECT_FLOAT_EQ(power->min, 0.0f);
    EXPECT_FLOAT_EQ(power->max, 10.0f);
    EXPECT_EQ(std::string(power->tooltip ? power->tooltip : ""), "how hard it hits");
    ASSERT_NE(ammo, nullptr);
    EXPECT_TRUE(ammo->flags & ReadOnly);                            // Runtime() : shown, not saved

    // What is saved : the defaults written after the declarations
    const ordered_json saved = declared->Serialize();
    EXPECT_FLOAT_EQ(saved["power"].get<float>(), 3.0f);
    EXPECT_FLOAT_EQ(saved["aim"]["z"].get<float>(), -1.0f);
    EXPECT_EQ(saved["label"].get<std::string>(), "declared");
    EXPECT_FALSE(saved.contains("ammo"));
    EXPECT_FALSE(saved.contains("secret"));
    EXPECT_FALSE(saved.contains("plain"));

    // Loaded, and carried into the copy of the actor that play mode makes
    declared->Deserialize(json::parse(R"({"active":true,"power":7.0,"aim":{"x":1,"y":2,"z":3},"label":"changed"})"));
    auto copy = actor->Clone();
    ASSERT_NE(copy, nullptr);
    const ordered_json copied = copy->GetComponents()[1]->Serialize();
    EXPECT_FLOAT_EQ(copied["power"].get<float>(), 7.0f);
    EXPECT_FLOAT_EQ(copied["aim"]["y"].get<float>(), 2.0f);
    EXPECT_EQ(copied["label"].get<std::string>(), "changed");
    EXPECT_EQ(Rec()->errorCount, 0u);
}

namespace {
    // The engine's own components, which the typed handles of the SDK reach through shard.actors' field functions
    class TypedComponentsTest : public GamePhysicsTest {
    protected:
        void StartWithEngineComponents()
        {
            Rendering::RegisterRenderingModule();
            Physics::RegisterPhysicsModule();
            Audio::RegisterAudioModule();
            Start();
        }

        std::shared_ptr<Objects::Actor> Root(const char* name)
        {
            for (auto& [id, actor] : world->GetRootActors()) if (actor->GetName() == name) return actor;
            return nullptr;
        }

        static ordered_json Saved(const std::shared_ptr<Objects::Actor>& actor, const char* type)
        {
            for (const auto& component : actor->GetComponents()) {
                const ordered_json saved = component->Serialize();
                if (saved.value("type", std::string()) == type) return saved;
            }
            return ordered_json();
        }

        const ShardActorsApi* Actors()
        {
            const void* table = nullptr;
            const char* id = SHARD_IFACE_ACTORS;
            EXPECT_EQ(GetCHostApi()->query_interface(ShardStr{ id, std::strlen(id) }, 1, 1, &table), SHARD_OK);
            return static_cast<const ShardActorsApi*>(table);
        }

        static ShardStr S(const std::string& text) { return ShardStr{ text.data(), text.size() }; }
    };
}

TEST_F(TypedComponentsTest, AScriptSetsTheFieldsOfTheEnginesComponentsOneByOne)
{
    StartWithEngineComponents();
    auto builder = NewActor("Builder");
    ASSERT_NE(builder->AddComponentByName(Type("Builder")), nullptr);
    world->Play();
    EXPECT_EQ(Rec()->errorCount, 0u);

    auto built = Root("Built");
    ASSERT_NE(built, nullptr);

    // A light : every setter wrote its own field
    const ordered_json light = Saved(built, "light");
    ASSERT_FALSE(light.is_null());
    EXPECT_EQ(light["lightType"].get<std::string>(), "Spot");
    EXPECT_FLOAT_EQ(light["intensity"].get<float>(), 7.5f);
    EXPECT_FLOAT_EQ(light["color"]["b"].get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(light["color"]["r"].get<float>(), 0.25f);
    EXPECT_NEAR(light["innerCutoff"].get<float>(), 10.0f, 1e-3f);       // the light keeps angles in radians : a round trip
    EXPECT_NEAR(light["outerCutoff"].get<float>(), 30.0f, 1e-3f);
    EXPECT_FALSE(light["castShadows"].get<bool>());
    EXPECT_FLOAT_EQ(Export<float (*)(const char*)>("game_float")("lightIntensity"), 7.5f);      // read back through the handle

    const ordered_json model = Saved(built, "model");
    ASSERT_FALSE(model.is_null());
    EXPECT_FALSE(model["staticLighting"].get<bool>());
    EXPECT_EQ(model["lightmapResolution"].get<int>(), 256);

    // A body : its shapes, then its motion type, whose body exists at once, so the velocity was taken
    EXPECT_EQ(Stat("shapeCount"), 2);
    EXPECT_EQ(Stat("shapeCountAfterRemove"), 1);
    EXPECT_EQ(Stat("velocitySet"), 1);
    auto body = built->GetComponent<Objects::Components::PhysicsBody>();
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->GetShapeCount(), 1u);
    EXPECT_EQ(body->GetShapeType(0), Physics::PhysicsShape::SPHERE);
    EXPECT_FLOAT_EQ(body->GetShapeParams<Objects::Components::SphereParams>(0).radius, 0.25f);
    EXPECT_EQ(body->GetMotionType(), JPH::EMotionType::Dynamic);
    EXPECT_FALSE(body->GetBodyID().IsInvalid());
    const glm::vec3 velocity = body->GetLinearVelocity();
    EXPECT_NEAR(velocity.x, 1.0f, 1e-4f);                           // a friction changed after the velocity did not rebuild the body
    EXPECT_NEAR(velocity.y, 2.0f, 1e-4f);
    EXPECT_NEAR(velocity.z, 3.0f, 1e-4f);
    EXPECT_NEAR(body->GetFriction(), 0.5f, 1e-5f);
    EXPECT_NEAR(body->GetMass(), 12.0f, 1e-3f);

    // A typo or a value of the wrong kind is refused, with the script told (it caught both)
    EXPECT_EQ(Stat("typedRefused"), 2);
    EXPECT_EQ(Stat("noListener"), 1);
    EXPECT_EQ(Stat("hasListener"), 1);
    EXPECT_NEAR(Export<float (*)(const char*)>("game_float")("listenerDoppler"), 2.5f, 1e-4f);
    EXPECT_EQ(Stat("hasListenerAfterRemove"), 0);
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(TypedComponentsTest, TheFieldFunctionsReadSetAndEraseByPath)
{
    StartWithEngineComponents();
    auto actor = NewActor("Subject");
    ASSERT_NE(actor->AddComponentByName("physics_body"), nullptr);
    ASSERT_NE(actor->AddComponentByName("light"), nullptr);
    const ShardActorsApi* api = Actors();
    ASSERT_NE(api, nullptr);
    const ShardActor handle = CAbi::ActorHandle(actor);
    auto read = [&](const char* type, const char* path) {
        std::string out;
        char buffer[512] = {};
        uint64_t size = 0;
        if (api->get_component_field(handle, S(type), S(path), buffer, sizeof(buffer), &size) == SHARD_OK) out.assign(buffer, size_t(size));
        return out;
    };

    // A field, its value as JSON
    EXPECT_EQ(api->set_component_field(handle, S("light"), S("intensity"), S("2.5")), SHARD_OK);
    EXPECT_EQ(read("light", "intensity"), "2.5");
    EXPECT_EQ(api->set_component_field(handle, S("light"), S("lightType"), S("\"Point\"")), SHARD_OK);
    EXPECT_EQ(read("light", "lightType"), "\"Point\"");
    EXPECT_EQ(api->set_component_field(handle, S("light"), S("color"), S("{\"r\":0.5,\"g\":0.25,\"b\":1}")), SHARD_OK);
    EXPECT_EQ(read("light", "color/g"), "0.25");                    // a path into it

    // Refusals : the other fields are left alone
    EXPECT_EQ(api->set_component_field(handle, S("light"), S("intensity"), S("\"bright\"")), SHARD_E_INVALID_ARG);
    EXPECT_EQ(api->set_component_field(handle, S("light"), S("lightType"), S("\"Laser\"")), SHARD_E_INVALID_ARG);
    EXPECT_EQ(api->set_component_field(handle, S("light"), S("intensity"), S("{not json")), SHARD_E_INVALID_ARG);
    EXPECT_EQ(api->set_component_field(handle, S("light"), S("nosuchfield"), S("1")), SHARD_E_NOT_FOUND);
    EXPECT_EQ(api->set_component_field(handle, S("audio_listener"), S("dopplerFactor"), S("2")), SHARD_E_NOT_FOUND);      // the actor has no listener
    EXPECT_EQ(api->set_component_field(handle, S("light"), S(""), S("1")), SHARD_E_INVALID_ARG);
    EXPECT_EQ(read("light", "intensity"), "2.5");
    EXPECT_EQ(read("light", "nosuchfield"), "");

    // The active state is a field of every component
    EXPECT_EQ(read("light", "active"), "true");
    EXPECT_EQ(api->set_component_field(handle, S("light"), S("active"), S("false")), SHARD_OK);
    EXPECT_EQ(read("light", "active"), "false");

    // Arrays : an element, appending with "-", and erasing
    EXPECT_EQ(api->set_component_field(handle, S("physics_body"), S("shapes/-"),
        S("{\"shape\":\"sphere\",\"params\":{\"radius\":2}}")), SHARD_OK);
    EXPECT_EQ(read("physics_body", "shapes/1/shape"), "\"sphere\"");
    EXPECT_EQ(api->set_component_field(handle, S("physics_body"), S("shapes/1/params/radius"), S("3")), SHARD_OK);
    EXPECT_EQ(read("physics_body", "shapes/1/params/radius"), "3.0");
    EXPECT_EQ(api->set_component_field(handle, S("physics_body"), S("shapes/9/params/radius"), S("3")), SHARD_E_NOT_FOUND);
    EXPECT_EQ(api->erase_component_field(handle, S("physics_body"), S("shapes/1")), SHARD_OK);
    EXPECT_EQ(read("physics_body", "shapes/1"), "");
    EXPECT_EQ(api->erase_component_field(handle, S("physics_body"), S("shapes/1")), SHARD_E_NOT_FOUND);
    EXPECT_EQ(api->erase_component_field(handle, S("physics_body"), S("friction")), SHARD_E_INVALID_ARG);       // a field is set, not erased

    // A stale handle is refused like everywhere else
    const ShardActor gone = handle;
    actor->Destroy();
    EXPECT_EQ(api->set_component_field(gone, S("light"), S("intensity"), S("1")), SHARD_E_STALE_HANDLE);
}

// ABI 1.7 : the interfaces a plugin of a particle system, a navigation mesh, voxels, a UI, an animation system, a network layer or a visual
// scripting editor builds on (tests/plugin_fixtures/game_scripts/src/platform_demo.cpp), through the SDK's wrappers
TEST_F(GamePhysicsTest, AScriptRunsInEditModeAndIsToldOfItsFieldChanges)
{
    Start();
    auto actor = NewActor("Probe");
    auto script = actor->AddComponentByName(Type("PlatformProbe"));
    ASSERT_NE(script, nullptr);
    auto stat = Export<double (*)(const char*)>("plat_stat");
    EXPECT_EQ(stat("editTicks"), -1000.0);

    world->EditTick();
    world->EditTick();
    EXPECT_EQ(stat("editTicks"), 2.0);                              // the world is not playing : the edit-mode script runs
    world->Play();
    world->Stop();

    EXPECT_TRUE(script->ApplyField("speed", 4.0));
    EXPECT_EQ(stat("fieldChanges"), 1.0);
    EXPECT_EQ(stat("lastField"), 1.0);                              // "speed" is the second field
    EXPECT_TRUE(script->ApplyField("level", 9));
    EXPECT_EQ(stat("lastField"), 0.0);
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(GamePhysicsTest, TheNewInterfacesAnswerThroughTheSdk)
{
    Start();
    auto ground = Body("Ground", glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f), JPH::EMotionType::Static);
    auto actor = NewActor("Probe");
    auto script = actor->AddComponentByName(Type("PlatformProbe"));
    ASSERT_NE(script, nullptr);
    script->Deserialize(json::parse(R"({"active":true,"level":8})"));
    auto stat = Export<double (*)(const char*)>("plat_stat");

    world->Play();
    EXPECT_EQ(stat("level"), 8.0);
    EXPECT_EQ(stat("fieldLevel"), 0.0);
    EXPECT_EQ(stat("fieldSpeed"), 1.0);
    EXPECT_EQ(stat("fieldNone"), -1.0);

    // Jobs
    EXPECT_GE(stat("workers"), 1.0);
    EXPECT_EQ(stat("jobRan"), 42.0);
    EXPECT_EQ(stat("parallelSum"), 332833500.0);                    // sum of i*i for i < 1000
    EXPECT_EQ(stat("asyncCount"), 500.0);
    EXPECT_EQ(stat("postedToMain"), -1000.0);                       // waits for the start of a frame
    pm().BeginFrame(1);
    EXPECT_EQ(stat("postedToMain"), 1.0);
    pm().EndFrame(1);

    // Time
    EXPECT_NEAR(stat("fixedDt"), 1.0 / 60.0, 1e-6);
    EXPECT_EQ(stat("timeScale"), 1.0);
    EXPECT_EQ(stat("hashStable"), 1.0);

    // Reflection
    EXPECT_EQ(stat("reflectSum"), 3.5);
    EXPECT_EQ(stat("reflectMissing"), 0.0);
    EXPECT_EQ(stat("reflectListed"), 1.0);
    EXPECT_GE(stat("reflectComponents"), 1.0);
    EXPECT_EQ(stat("reflectDuplicate"), 1.0);                       // a name taken is refused

    // Sockets : a datagram over the loopback
    EXPECT_GT(stat("udpPort"), 0.0);
    EXPECT_EQ(stat("udpDatagrams"), 1.0);
    EXPECT_STREQ(Export<const char* (*)()>("plat_text")(), "ping");
    EXPECT_EQ(stat("udpClosed"), 1.0);
    EXPECT_EQ(stat("tcpAccepted"), 1.0);                            // a stream : accepted, connected, bytes each way, and the close is seen
    EXPECT_EQ(stat("tcpConnected"), 1.0);
    EXPECT_EQ(stat("tcpToServer"), 1.0);
    EXPECT_EQ(stat("tcpToClient"), 1.0);
    EXPECT_EQ(stat("tcpServerSawClose"), 1.0);

    // Assets : nothing of that name
    EXPECT_EQ(stat("assetResolved"), 0.0);
    EXPECT_EQ(stat("assetExists"), 0.0);
    EXPECT_EQ(stat("assetInfo"), 0.0);
    EXPECT_EQ(stat("assetMesh"), 0.0);
    EXPECT_EQ(stat("assetPathOf"), 0.0);

    // Console commands run from a plugin
    EXPECT_EQ(stat("commandOk"), 1.0);
    EXPECT_EQ(stat("commandFound"), 1.0);
    EXPECT_EQ(stat("commandRefused"), 1.0);
    EXPECT_EQ(stat("commandMissing"), 1.0);
    EXPECT_EQ(stat("commandOutput"), 1.0);
    EXPECT_EQ(stat("undoPushed"), 0.0);                             // no editor : refused

    // Actors : the stable id finds the actor again
    EXPECT_EQ(stat("stableId"), 1.0);
    EXPECT_EQ(stat("stableRoundTrip"), 1.0);
    EXPECT_EQ(stat("inSphere"), 2.0);                               // the actor itself and the ground, whose center is just under it
    EXPECT_EQ(stat("inBox"), 2.0);

    // Physics queries : the ground, then a collider of the plugin that has no actor
    EXPECT_EQ(stat("rayAll"), 1.0);
    EXPECT_NEAR(stat("rayAllDistance"), 5.0, 0.05);
    EXPECT_EQ(stat("rayAllActor"), 1.0);
    EXPECT_GE(stat("overlapSphere"), 1.0);
    EXPECT_EQ(stat("overlapFar"), 0.0);
    EXPECT_EQ(stat("sweep"), 1.0);
    EXPECT_NEAR(stat("sweepDistance"), 4.5, 0.1);                   // the sphere's radius stops it above the face
    EXPECT_EQ(stat("rayCollidersOnly"), 0.0);                       // the ground is a body, not a collider
    EXPECT_EQ(stat("colliderMade"), 1.0);
    EXPECT_EQ(stat("colliderPose"), 1.0);
    physics.StepSimulation(1.0f / 60.0f, false);
    pm().BeginFrame(2);
    physics.StepSimulation(1.0f / 60.0f, false);
    pm().EndFrame(2);
    auto rayCollider = Export<int (*)()>("plat_ray_collider");
    EXPECT_EQ(rayCollider(), 1);
    EXPECT_NEAR(stat("colliderDistance"), 7.0, 0.05);               // the top face is at y = 3
    EXPECT_EQ(stat("colliderUserData"), 77.0);
    EXPECT_EQ(stat("colliderHandle"), 1.0);
    EXPECT_EQ(stat("colliderHasActor"), 0.0);
    Export<void (*)()>("plat_release_collider")();
    physics.StepSimulation(1.0f / 60.0f, false);
    EXPECT_EQ(rayCollider(), 0);                                    // released : gone from the world

    EXPECT_EQ(stat("meshVertices"), 4.0);
    EXPECT_EQ(stat("meshIndices"), 6.0);
    EXPECT_EQ(stat("meshCreated"), 1.0);                            // resources are CPU-side until a renderer draws them
    EXPECT_EQ(stat("textureCreated"), 1.0);
    EXPECT_EQ(stat("textureWidth"), 2.0);
    EXPECT_EQ(stat("debugEnabled"), 0.0);                           // nothing to draw on without a renderer : the calls are accepted and dropped
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(GamePhysicsTest, AComponentIsToldWhenItJoinsIsEditedAndLeaves)
{
    Start();
    auto actor = NewActor("Lamp");
    auto stat = Export<double (*)(const char*)>("plat_stat");
    auto beacon = actor->AddComponentByName(Type("Beacon"));
    ASSERT_NE(beacon, nullptr);
    EXPECT_EQ(stat("beaconAdded"), 1.0);
    EXPECT_EQ(stat("beaconActorOk"), 1.0);
    EXPECT_EQ(stat("beaconRemoved"), -1000.0);

    EXPECT_TRUE(beacon->ApplyField("power", 7));
    EXPECT_EQ(stat("beaconChanged"), 1.0);
    EXPECT_EQ(stat("beaconField"), 0.0);                            // "power" is the first field
    EXPECT_EQ(stat("beaconPowerChanged"), 7.0);                     // the block already holds the new value

    actor->RemoveComponent(beacon);
    EXPECT_EQ(stat("beaconRemoved"), 1.0);
    EXPECT_EQ(stat("beaconPowerAtRemoval"), 7.0);                   // the block is intact when it is told
    EXPECT_EQ(Rec()->errorCount, 0u);
}

namespace {
    struct Undoable {
        std::string label;
        std::function<void()> undo, redo;
    };
    std::vector<Undoable>& Undos()
    {
        static std::vector<Undoable> undos;
        return undos;
    }
}

TEST_F(GamePhysicsTest, TheEditorKeepsWhatAPluginAddsToItsContentBrowserAndUndoHistory)
{
    Undos().clear();
    static const EditorUiBackend backend = [] {
        EditorUiBackend b = *EditorFakes::Backend();
        b.pushUndo = [](const std::string& label, std::function<void()> undo, std::function<void()> redo) { Undos().push_back({ label, std::move(undo), std::move(redo) }); };
        return b;
    }();
    SetEditorUiBackend(&backend);
    Start();
    auto stat = Export<double (*)(const char*)>("plat_stat");

    // A double click on a file of the plugin's extension, and an entry of the Create menu
    const auto openers = pm().Extensions().Get<EditorAssetOpenerItem>("editor.assetopener");
    ASSERT_EQ(openers.size(), 1u);
    EXPECT_EQ(openers[0]->extension, ".beacon");
    openers[0]->open("levels/lamp.beacon");
    EXPECT_EQ(stat("opened"), 1.0);
    EXPECT_STREQ(Export<const char* (*)()>("plat_text")(), "levels/lamp.beacon");
    const auto templates = pm().Extensions().Get<EditorAssetTemplateItem>("editor.assettemplate");
    ASSERT_EQ(templates.size(), 1u);
    EXPECT_EQ(templates[0]->label, "Beacon");
    EXPECT_EQ(templates[0]->defaultName, "new_beacon.beacon");
    EXPECT_EQ(templates[0]->bytes, "{\"power\":3}");

    // The plugin's edit is in the editor's history, and its undo and redo reach the plugin
    auto actor = NewActor("Probe");
    ASSERT_NE(actor->AddComponentByName(Type("PlatformProbe")), nullptr);
    world->Play();
    EXPECT_EQ(stat("undoPushed"), 1.0);
    ASSERT_EQ(Undos().size(), 1u);
    EXPECT_EQ(Undos()[0].label, "probe edit");
    Undos()[0].undo();
    EXPECT_EQ(stat("undone"), 1.0);
    Undos()[0].redo();
    EXPECT_EQ(stat("redone"), 1.0);

    // Once the plugin is gone its history entries do nothing instead of calling into a library that left
    const Undoable kept = Undos()[0];
    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    kept.undo();
    kept.redo();
    EXPECT_TRUE(pm().Extensions().Get<EditorAssetOpenerItem>("editor.assetopener").empty());
    Undos().clear();
    SetEditorUiBackend(nullptr);
}

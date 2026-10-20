#include "engine/core/tags/tag_registry.hpp"
#include <gtest/gtest.h>

#include <thread>

#include "engine/core/ecs/registry.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_extension.hpp"
#include "plugins/plugin_test_utils.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using nlohmann::json;
using PluginTest::TempDir;

namespace {
    // What a test plugin does, by plugin id : one class serves them all
    struct Script {
        std::function<void(PluginContext&)> onLoad, onInitialize, onContentMounted, onEnable, onDisable, onUnload;
        std::function<void(Worlds::World&)> onWorldLoaded, onPlayStart, onPlayStop, onWorldUnloaded;
    };

    std::map<std::string, Script>& Scripts() { static std::map<std::string, Script> s; return s; }
    std::vector<std::string>& Log() { static std::vector<std::string> l; return l; }

    class Scripted final : public IModule {
    public:
        ModuleKind Kind() const override { return ModuleKind::Code; }
        void OnLoad(PluginContext& ctx) override { m_Id = ctx.Id(); Log().push_back("load " + m_Id); Call(&Script::onLoad, ctx); }
        void OnInitialize(PluginContext& ctx) override { Log().push_back("init " + m_Id); Call(&Script::onInitialize, ctx); }
        void OnContentMounted(PluginContext& ctx) override { Log().push_back("content " + m_Id); Call(&Script::onContentMounted, ctx); }
        void OnEnable(PluginContext& ctx) override { Log().push_back("enable " + m_Id); Call(&Script::onEnable, ctx); }
        void OnDisable(PluginContext& ctx) override { Log().push_back("disable " + m_Id); Call(&Script::onDisable, ctx); }
        void OnUnload(PluginContext& ctx) override { Log().push_back("unload " + m_Id); Call(&Script::onUnload, ctx); }
        void OnWorldLoaded(Worlds::World& w) override { Log().push_back("world-loaded " + m_Id); if (auto& f = Scripts()[m_Id].onWorldLoaded) f(w); }
        void OnWorldUnloaded(Worlds::World& w) override { Log().push_back("world-unloaded " + m_Id); if (auto& f = Scripts()[m_Id].onWorldUnloaded) f(w); }
        void OnPlayStart(Worlds::World& w) override { Log().push_back("play-start " + m_Id); if (auto& f = Scripts()[m_Id].onPlayStart) f(w); }
        void OnPlayStop(Worlds::World& w) override { Log().push_back("play-stop " + m_Id); if (auto& f = Scripts()[m_Id].onPlayStop) f(w); }
    private:
        void Call(std::function<void(PluginContext&)> Script::* member, PluginContext& ctx)
        {
            auto& f = Scripts()[m_Id].*member;
            if (f) f(ctx);
        }
        std::string m_Id;
    };

    IPlugin* MakeScripted() { return new Scripted(); }

    class ScriptedServer final : public ServerPlugin {
    public:
        void OnClientConnect(ClientInfo& client, ConnectDecision& decision) override
        {
            Log().push_back("connect " + client.name);
            if (client.name == "banned") decision.Refuse("banned player");
        }
        void OnServerStart(ServerContext& s) override { Log().push_back(std::string("server-start ") + (s.dedicated ? "dedicated" : "listen")); }
        void OnServerTick(ServerContext&, uint64_t tick) override { Log().push_back("tick " + std::to_string(tick)); }
    };
    IPlugin* MakeServer() { return new ScriptedServer(); }

    class ScriptedTool final : public ToolPlugin {
    public:
        bool OnToolTask(const std::string& task, const std::string&, std::string& message) override
        {
            if (task == "cook") { message = "no cooking today"; return false; }
            return true;
        }
    };
    IPlugin* MakeTool() { return new ScriptedTool(); }

    class ScriptedEnginePlugin final : public EnginePlugin {
    public:
        void OnFrameBegin(uint64_t frame) override { begin = frame; }
        void OnFrameEnd(uint64_t frame) override { end = frame; }
        static inline uint64_t begin = 0, end = 0;
    };
    IPlugin* MakeEnginePlugin() { return new ScriptedEnginePlugin(); }

    class ScriptedEditor final : public EditorPlugin {
    public:
        void OnEditorInit(EditorContext& editor) override
        {
            Log().push_back("editor-init");
            AddEditorPanel(editor.plugin, "Scripted panel", [](bool*) {});
        }
        void OnEditorShutdown(EditorContext&) override { Log().push_back("editor-shutdown"); }
        void OnSelectionChanged(const std::vector<std::string>& actors) override
        {
            Log().push_back("selection " + std::to_string(actors.size()));
        }
    };
    IPlugin* MakeEditor() { return new ScriptedEditor(); }

    std::string ModuleManifest(const std::string& id, std::function<void(json&)> edit = nullptr)
    {
        json j = PluginTest::Manifest(id);
        j["kind"] = "module";
        j["moduleKind"] = "code";
        j["simulation"] = { { "affects", true } };
        if (edit) edit(j);
        return j.dump();
    }

    std::string PluginManifestText(const std::string& id, std::function<void(json&)> edit = nullptr)
    {
        json j = PluginTest::Manifest(id);
        if (edit) edit(j);
        return j.dump();
    }

    bool Logged(const std::string& line) { return std::find(Log().begin(), Log().end(), line) != Log().end(); }
    long IndexOf(const std::string& line) { auto it = std::find(Log().begin(), Log().end(), line); return it == Log().end() ? -1 : long(it - Log().begin()); }

    class PluginManagerTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            PluginManager::Get().ResetForTests();
            Scripts().clear();
            Log().clear();
            config.host = Host::Headless;
            config.registerConsoleCommands = false;
        }
        void TearDown() override { PluginManager::Get().ResetForTests(); }

        PluginManager& pm() { return PluginManager::Get(); }

        void AddScripted(const std::string& manifest) { pm().AddStaticPlugin(manifest, &MakeScripted); }

        bool Start(std::string* error = nullptr) { return pm().Startup(config, error); }

        void BringUpEngine()
        {
            pm().OnEngineSystemsReady(nullptr, &scheduler, &events);
            pm().OnEngineInitialized();
        }

        void RunFrame(uint64_t frame)
        {
            pm().BeginFrame(frame);
            Core::Ecs::Registry registry;
            Core::Ecs::Scheduler::FrameTiming timing;
            timing.deltaTime = 0.016f;
            scheduler.RunFrame(registry, nullptr, timing);
            pm().EndFrame(frame);
        }

        PluginRecord* Rec(const std::string& id) { return pm().Find(id); }
        PluginState State(const std::string& id) { auto* r = Rec(id); return r ? r->state : PluginState::Unloaded; }

        PluginManagerConfig config;
        Core::Ecs::Scheduler scheduler;
        Events::EventDispatcher events;
    };
}

TEST_F(PluginManagerTest, RunsTheLifecycleInOrder)
{
    AddScripted(ModuleManifest("b.second", [](json& j) { j["dependencies"] = json::array({ "a.first" }); }));
    AddScripted(ModuleManifest("a.first"));
    ASSERT_TRUE(Start());
    EXPECT_EQ(State("a.first"), PluginState::Initialized);

    // Every plugin loads, then every plugin initializes ; enabling waits for the engine
    EXPECT_EQ(Log(), (std::vector<std::string>{ "load a.first", "load b.second", "init a.first", "init b.second" }));

    BringUpEngine();
    EXPECT_EQ(State("a.first"), PluginState::Enabled);
    EXPECT_EQ(State("b.second"), PluginState::Enabled);
    EXPECT_LT(IndexOf("enable a.first"), IndexOf("enable b.second"));

    pm().OnEngineDestroying();
    EXPECT_LT(IndexOf("disable b.second"), IndexOf("disable a.first"));      // the reverse
    EXPECT_EQ(State("a.first"), PluginState::Disabled);

    pm().Shutdown();
    EXPECT_TRUE(Logged("unload a.first"));
    EXPECT_TRUE(Logged("unload b.second"));
    EXPECT_FALSE(pm().IsStarted());
}

TEST_F(PluginManagerTest, TheEnginesOwnPartsAreModulesToo)
{
    ASSERT_TRUE(Start());
    for (const char* id : { "shard.core", "shard.physics", "shard.audio", "shard.renderer", "shard.assets", "shard.world" }) {
        ASSERT_NE(Rec(id), nullptr) << id;
        EXPECT_TRUE(Rec(id)->isStatic);
        EXPECT_EQ(Rec(id)->manifest.moduleKind, ModuleKind::Engine);
    }
    // and a plugin can depend on them
    pm().ResetForTests();
    AddScripted(PluginManifestText("needs.physics", [](json& j) { j["dependencies"] = json::array({ "shard.physics" }); }));
    ASSERT_TRUE(Start());
    EXPECT_EQ(State("needs.physics"), PluginState::Initialized);
}

TEST_F(PluginManagerTest, AFailingPluginTakesItsDependentsAndNobodyElse)
{
    Scripts()["bad"].onLoad = [](PluginContext&) { throw std::runtime_error("cannot start"); };
    AddScripted(ModuleManifest("bad"));
    AddScripted(ModuleManifest("needs.bad", [](json& j) { j["dependencies"] = json::array({ "bad" }); }));
    AddScripted(ModuleManifest("fine"));
    ASSERT_TRUE(Start());
    EXPECT_EQ(State("bad"), PluginState::Failed);
    EXPECT_NE(Rec("bad")->error.find("cannot start"), std::string::npos);
    EXPECT_EQ(State("needs.bad"), PluginState::Failed);
    EXPECT_NE(Rec("needs.bad")->error.find("dependency"), std::string::npos);
    EXPECT_EQ(State("fine"), PluginState::Initialized);
}

TEST_F(PluginManagerTest, AFailingPluginGivesBackWhatItRegisteredBeforeFailing)
{
    Scripts()["half"].onLoad = [](PluginContext& ctx) {
        ctx.Commands().Add("half.cmd", "", [](const std::vector<std::string>&) { return Core::CommandResult{}; });
        throw std::runtime_error("halfway");
    };
    AddScripted(ModuleManifest("half"));
    ASSERT_TRUE(Start());
    EXPECT_EQ(State("half"), PluginState::Failed);
    EXPECT_FALSE(Core::CVarRegistry::Global().Execute("half.cmd").ok);
}

TEST_F(PluginManagerTest, ARequiredPluginThatFailsStopsTheStart)
{
    TempDir project;
    project.Write("p/project.json", R"({ "plugins": [ { "id": "must", "required": true } ] })");
    config.projectFile = project.Sub("p/project.json");
    config.readOnly = true;
    Scripts()["must"].onLoad = [](PluginContext&) { throw std::runtime_error("nope"); };
    AddScripted(ModuleManifest("must"));
    std::string error;
    EXPECT_FALSE(Start(&error));
    EXPECT_NE(error.find("must"), std::string::npos) << error;
}

TEST_F(PluginManagerTest, AMissingRequiredPluginStopsTheStart)
{
    TempDir project;
    project.Write("p/project.json", R"({ "plugins": [ { "id": "absent.plugin", "required": true } ] })");
    config.projectFile = project.Sub("p/project.json");
    config.readOnly = true;
    AddScripted(ModuleManifest("other"));
    std::string error;
    EXPECT_FALSE(Start(&error));
    EXPECT_NE(error.find("absent.plugin"), std::string::npos) << error;
}

TEST_F(PluginManagerTest, ComponentsCommandsAndSystemsComeAndGoWithTheirPlugin)
{
    static int ran = 0;
    ran = 0;
    Scripts()["owner"].onLoad = [](PluginContext& ctx) {
        ctx.Components().Register("owner/Thing", +[]() -> std::shared_ptr<Objects::Components::Component> { return nullptr; });
        ctx.Commands().Add("owner.ping", "", [](const std::vector<std::string>&) { return Core::CommandResult{ true, "pong" }; });
        Core::Ecs::SystemDesc d;
        d.name = "tick";
        d.phase = Core::Ecs::Phase::Update;
        d.run = [](Core::Ecs::SystemContext&) { ++ran; };
        ctx.Systems().Add(std::move(d));
    };
    AddScripted(ModuleManifest("owner"));
    ASSERT_TRUE(Start());
    auto& components = Objects::Components::GetComponentRegistry();
    EXPECT_TRUE(components.IsRegistered("owner/Thing"));
    EXPECT_EQ(Core::CVarRegistry::Global().Execute("owner.ping").output, "pong");

    BringUpEngine();
    RunFrame(1);
    EXPECT_EQ(ran, 1);
    EXPECT_TRUE(scheduler.Has("owner/tick"));

    std::string error;
    ASSERT_TRUE(pm().Disable("owner", true, &error)) << error;
    EXPECT_EQ(State("owner"), PluginState::Disabled);
    EXPECT_FALSE(components.IsRegistered("owner/Thing"));
    EXPECT_FALSE(Core::CVarRegistry::Global().Execute("owner.ping").ok);
    EXPECT_FALSE(scheduler.Has("owner/tick"));
    RunFrame(2);
    EXPECT_EQ(ran, 1);

    ASSERT_TRUE(pm().Enable("owner", &error)) << error;
    EXPECT_EQ(State("owner"), PluginState::Enabled);
    EXPECT_TRUE(components.IsRegistered("owner/Thing"));
    EXPECT_TRUE(scheduler.Has("owner/tick"));
    RunFrame(3);
    EXPECT_EQ(ran, 2);
    EXPECT_EQ(Log().front(), "load owner");
    EXPECT_EQ(std::count(Log().begin(), Log().end(), "load owner"), 2);          // OnLoad ran again for the second enable
}

TEST_F(PluginManagerTest, DisableStopsTheDependentsFirstOrRefuses)
{
    AddScripted(ModuleManifest("base"));
    AddScripted(ModuleManifest("on.base", [](json& j) { j["dependencies"] = json::array({ "base" }); }));
    ASSERT_TRUE(Start());
    BringUpEngine();

    std::string error;
    EXPECT_FALSE(pm().Disable("base", false, &error));
    EXPECT_NE(error.find("on.base"), std::string::npos);
    EXPECT_EQ(State("base"), PluginState::Enabled);

    ASSERT_TRUE(pm().Disable("base", true, &error)) << error;
    EXPECT_EQ(State("on.base"), PluginState::Disabled);
    EXPECT_EQ(State("base"), PluginState::Disabled);
    EXPECT_LT(IndexOf("disable on.base"), IndexOf("disable base"));

    // Enabling the dependent brings what it needs back first
    ASSERT_TRUE(pm().Enable("on.base", &error)) << error;
    EXPECT_EQ(State("base"), PluginState::Enabled);
    EXPECT_EQ(State("on.base"), PluginState::Enabled);
}

TEST_F(PluginManagerTest, AModuleWithoutComponentsCanBeDisabledWhileAWorldIsLoaded)
{
    AddScripted(ModuleManifest("plain"));
    ASSERT_TRUE(Start());
    BringUpEngine();
    PluginManager::Callbacks callbacks;
    callbacks.hasLoadedWorlds = [] { return true; };
    pm().SetCallbacks(callbacks);
    std::string error;
    EXPECT_TRUE(pm().Disable("plain", true, &error)) << error;
}

TEST_F(PluginManagerTest, EnginePartsCannotBeDisabled)
{
    ASSERT_TRUE(Start());
    std::string error;
    EXPECT_FALSE(pm().Disable("shard.physics", true, &error));
    EXPECT_FALSE(error.empty());
}

TEST_F(PluginManagerTest, ServicesLinkPluginsAndVanishWithTheProvider)
{
    static int value = 99;
    struct Api { int* v; };
    static Api api{ &value };
    static ServiceRef<Api> held;
    held = ServiceRef<Api>();

    Scripts()["provider"].onLoad = [](PluginContext& ctx) { ctx.Services().Provide<Api>("shared.api", 1, &api); };
    Scripts()["consumer"].onInitialize = [](PluginContext& ctx) { held = ctx.Services().Get<Api>("shared.api", "^1.0"); };
    AddScripted(ModuleManifest("consumer", [](json& j) { j["dependencies"] = json::array({ "service:shared.api" }); }));
    AddScripted(ModuleManifest("provider", [](json& j) { j["provides"] = json::array({ json{ { "service", "shared.api" }, { "version", "1.0" } } }); }));
    ASSERT_TRUE(Start());
    BringUpEngine();
    ASSERT_TRUE(held);
    EXPECT_EQ(*held->v, 99);

    std::string error;
    ASSERT_TRUE(pm().Disable("provider", true, &error)) << error;
    EXPECT_FALSE(held);
    EXPECT_EQ(State("consumer"), PluginState::Disabled);
}

TEST_F(PluginManagerTest, HooksCanCancelPlayWorldLoadAndSave)
{
    Scripts()["guard"].onLoad = [](PluginContext& ctx) {
        ctx.Hooks().On<Hooks::PrePlay>([](Hooks::PrePlay& h) { h.Cancel("not now"); });
        ctx.Hooks().On<Hooks::PreWorldLoad>([](Hooks::PreWorldLoad& h) { if (h.worldName == "secret.world") h.Cancel("private"); });
        ctx.Hooks().On<Hooks::PreSave>([](Hooks::PreSave& h) { if (h.what == "locked.world") h.Cancel("read only"); });
    };
    AddScripted(ModuleManifest("guard"));
    ASSERT_TRUE(Start());
    BringUpEngine();
    EXPECT_FALSE(pm().BeforePlay());
    EXPECT_TRUE(pm().BeforeWorldLoad("open.world"));
    EXPECT_FALSE(pm().BeforeWorldLoad("secret.world"));
    EXPECT_TRUE(pm().BeforeSave("free.world"));
    EXPECT_FALSE(pm().BeforeSave("locked.world"));
    // the world layer asks the same question before writing
    EXPECT_FALSE(Worlds::AllowWorldSave("locked.world"));
    EXPECT_TRUE(Worlds::AllowWorldSave("free.world"));

    ASSERT_TRUE(pm().Disable("guard"));
    EXPECT_TRUE(pm().BeforePlay());
}

TEST_F(PluginManagerTest, FrameHooksAndEnginePluginCallbacksRun)
{
    int pre = 0, post = 0;
    pm().AddStaticPlugin(PluginManifestText("framer"), &MakeEnginePlugin);
    Scripts()["hooker"].onLoad = [&](PluginContext& ctx) {
        ctx.Hooks().On<Hooks::PreFrame>([&](Hooks::PreFrame& h) { pre = int(h.frame); });
        ctx.Hooks().On<Hooks::PostFrame>([&](Hooks::PostFrame& h) { post = int(h.frame); });
    };
    AddScripted(ModuleManifest("hooker"));
    ASSERT_TRUE(Start());
    BringUpEngine();
    RunFrame(7);
    EXPECT_EQ(pre, 7);
    EXPECT_EQ(post, 7);
    EXPECT_EQ(ScriptedEnginePlugin::begin, 7u);
    EXPECT_EQ(ScriptedEnginePlugin::end, 7u);
}

TEST_F(PluginManagerTest, ASystemThatKeepsThrowingSwitchesItsPluginOff)
{
    Scripts()["flaky"].onLoad = [](PluginContext& ctx) {
        Core::Ecs::SystemDesc d;
        d.name = "boom";
        d.phase = Core::Ecs::Phase::Update;
        d.run = [](Core::Ecs::SystemContext&) { throw std::runtime_error("boom"); };
        ctx.Systems().Add(std::move(d));
    };
    AddScripted(ModuleManifest("flaky"));
    AddScripted(ModuleManifest("steady"));
    ASSERT_TRUE(Start());
    BringUpEngine();
    for (uint64_t f = 1; f <= 12; ++f) RunFrame(f);
    EXPECT_EQ(State("flaky"), PluginState::Failed);
    EXPECT_FALSE(scheduler.Has("flaky/boom"));
    EXPECT_EQ(State("steady"), PluginState::Enabled);
    EXPECT_GE(Rec("flaky")->errorCount, 10u);
}

TEST_F(PluginManagerTest, ASystemOverItsBudgetIsFlagged)
{
    Scripts()["slow"].onLoad = [](PluginContext& ctx) {
        Core::Ecs::SystemDesc d;
        d.name = "work";
        d.phase = Core::Ecs::Phase::Update;
        d.run = [](Core::Ecs::SystemContext&) { std::this_thread::sleep_for(std::chrono::milliseconds(3)); };
        ctx.Systems().Add(std::move(d));
    };
    AddScripted(ModuleManifest("slow", [](json& j) { j["budgets"] = { { "frameMs", 0.1 } }; }));
    ASSERT_TRUE(Start());
    BringUpEngine();
    for (uint64_t f = 1; f <= 5; ++f) RunFrame(f);
    pm().UpdateBudgets();
    EXPECT_TRUE(Rec("slow")->overBudget);
    EXPECT_GT(Rec("slow")->systemsMsAverage, 0.1);
}

TEST_F(PluginManagerTest, ProjectSettingsAndManifestDefaultsFeedTheCVars)
{
    TempDir project;
    project.Write("p/project.json", R"({ "plugins": [ { "id": "tuned", "settings": { "rate": 0.75, "com.test.tuned.name": "from project" } } ] })");
    config.projectFile = project.Sub("p/project.json");
    config.readOnly = true;
    Core::CVar<float>* rate = nullptr;
    Core::CVar<float>* gain = nullptr;
    Core::CVar<std::string>* name = nullptr;
    Scripts()["tuned"].onLoad = [&](PluginContext& ctx) {
        rate = ctx.CVars().Float("rate", 0.1f);
        gain = ctx.CVars().Float("gain", 0.1f);
        name = ctx.CVars().String("name", "default");
    };
    // the plugin's id is "tuned" so its variables are "tuned.rate" ; "com.test.tuned.name" is not one of them
    AddScripted(ModuleManifest("tuned", [](json& j) { j["settings"] = { { "gain", 2.5 } }; }));
    ASSERT_TRUE(Start());
    ASSERT_NE(rate, nullptr);
    EXPECT_FLOAT_EQ(rate->Get(), 0.75f);            // the project's value
    EXPECT_FLOAT_EQ(gain->Get(), 2.5f);             // the manifest's default
    EXPECT_EQ(name->Get(), "default");
    EXPECT_EQ(Core::CVarRegistry::Global().WinningLayer("tuned.rate"), Core::ConfigLayer::Project);
    EXPECT_EQ(Core::CVarRegistry::Global().WinningLayer("tuned.gain"), Core::ConfigLayer::Engine);
}

TEST_F(PluginManagerTest, TheConsoleListsAndControlsPlugins)
{
    config.registerConsoleCommands = true;
    AddScripted(ModuleManifest("listed"));
    ASSERT_TRUE(Start());
    BringUpEngine();
    auto& console = Core::CVarRegistry::Global();
    EXPECT_NE(console.Execute("plugin.list").output.find("listed"), std::string::npos);
    EXPECT_NE(console.Execute("plugin.info listed").output.find("listed"), std::string::npos);
    EXPECT_TRUE(console.Execute("plugin.disable listed").ok);
    EXPECT_EQ(State("listed"), PluginState::Disabled);
    EXPECT_TRUE(console.Execute("plugin.enable listed").ok);
    EXPECT_FALSE(console.Execute("plugin.enable ghost").ok);
    EXPECT_FALSE(console.Execute("plugin.reload listed").ok);          // hot reload is off
    EXPECT_FALSE(console.Execute("plugin.info").ok);
    EXPECT_TRUE(console.Execute("plugin.hash").ok);
    pm().OnEngineDestroying();
    pm().Shutdown();
    EXPECT_FALSE(console.Execute("plugin.list").ok);          // the commands left with the manager
}

TEST_F(PluginManagerTest, StaticPluginsDeclaredBeforeStartupAreFound)
{
    RegisterStaticPlugin(PluginManifestText("declared.early").c_str(), &MakeScripted);
    ASSERT_TRUE(Start());
    ASSERT_NE(Rec("declared.early"), nullptr);
    EXPECT_TRUE(Rec("declared.early")->isStatic);
    EXPECT_EQ(Rec("declared.early")->state, PluginState::Initialized);
}

// ---- Discovery ----

TEST_F(PluginManagerTest, FindsPluginsInFoldersAndReportsTheBrokenOnes)
{
    TempDir dir;
    dir.Write("plugins/good/shard-plugin.json", PluginTest::DatapackManifest("pack.good"));
    dir.Write("plugins/good/content/tables/t.json", "{}");
    dir.Write("plugins/broken/shard-plugin.json", "{ \"id\": \"Not Valid\" }");
    dir.Write("plugins/empty/readme.txt", "no manifest here");
    json editorOnly = PluginTest::Manifest("editor.thing");
    editorOnly["hosts"] = json::array({ "editor" });
    dir.Write("plugins/editor/shard-plugin.json", editorOnly.dump());
    config.searchPaths.push_back(dir.Sub("plugins"));
    ASSERT_TRUE(Start());
    EXPECT_NE(Rec("pack.good"), nullptr);
    EXPECT_EQ(Rec("editor.thing"), nullptr);
    bool brokenReported = false, wrongHost = false;
    for (const Rejection& r : pm().Rejections()) {
        if (r.reason.find("invalid manifest") != std::string::npos) brokenReported = true;
        if (r.id == "editor.thing" && r.kind == Rejection::Kind::WrongHost) wrongHost = true;
    }
    EXPECT_TRUE(brokenReported);
    EXPECT_TRUE(wrongHost);
}

TEST_F(PluginManagerTest, DiscoveryOrderDoesNotDependOnTheFileSystem)
{
    TempDir dir;
    for (const char* name : { "zeta", "alpha", "mid" })
        dir.Write(std::string("plugins/") + name + "/shard-plugin.json", PluginTest::DatapackManifest(std::string("pack.") + name));
    config.searchPaths.push_back(dir.Sub("plugins"));
    ASSERT_TRUE(Start());
    std::vector<std::string> order;
    for (auto& r : pm().Plugins()) if (!r->isStatic) order.push_back(r->manifest.id);
    EXPECT_EQ(order, (std::vector<std::string>{ "pack.alpha", "pack.mid", "pack.zeta" }));
}

TEST_F(PluginManagerTest, ProjectListTurnsPluginsOffAndPicksVersions)
{
    TempDir dir;
    dir.Write("pl/a1/shard-plugin.json", PluginTest::DatapackManifest("pack.v", "1.0.0"));
    dir.Write("pl/a2/shard-plugin.json", PluginTest::DatapackManifest("pack.v", "2.0.0"));
    dir.Write("pl/off/shard-plugin.json", PluginTest::DatapackManifest("pack.off"));
    dir.Write("proj/project.json", R"({ "pluginsFolder": "../pl", "plugins": [ { "id": "pack.v", "version": "^1.0" }, { "id": "pack.off", "enabled": false } ] })");
    config.projectFile = dir.Sub("proj/project.json");
    config.readOnly = true;
    ASSERT_TRUE(Start());
    ASSERT_NE(Rec("pack.v"), nullptr);
    EXPECT_EQ(Rec("pack.v")->manifest.version.ToString(), "1.0.0");
    EXPECT_EQ(Rec("pack.off"), nullptr);
}

// ---- Datapacks ----

TEST_F(PluginManagerTest, ADatapackMountsItsContentAndPatchesWorlds)
{
    TempDir dir;
    dir.Write("plugins/pack/shard-plugin.json", PluginTest::DatapackManifest("pack.arena"));
    dir.Write("plugins/pack/content/tables/weapons.json", R"({ "sword": { "damage": 5 } })");
    dir.Write("plugins/pack/content/lang/fr.json", R"({ "hello": "Bonjour" })");
    dir.Write("plugins/pack/content/materials/stone.mat", "stone");
    dir.Write("plugins/pack/content/components/health.json", R"({ "name": "Health", "fields": { "hp": { "type": "float", "default": 10 } } })");
    dir.Write("plugins/pack/content/patches/room.patch", R"({ "world": "room.world", "ops": [ { "op": "addActor", "actor": { "name": "Banner" } } ] })");
    config.searchPaths.push_back(dir.Sub("plugins"));
    ASSERT_TRUE(Start());
    BringUpEngine();
    ASSERT_EQ(State("pack.arena"), PluginState::Enabled);
    EXPECT_EQ(Rec("pack.arena")->manifest.tier, TrustTier::Data);

    EXPECT_EQ(pm().Content().LoadTable("weapons")["sword"]["damage"], 5);
    EXPECT_EQ(pm().Content().Translate("hello", "fr"), "Bonjour");
    EXPECT_TRUE(pm().Content().Exists("mod://pack.arena/materials/stone.mat"));
    ASSERT_EQ(pm().DeclaredComponents().size(), 1u);
    EXPECT_EQ(pm().DeclaredComponents()[0].component.name, "Health");

    // the patch reaches a world through the filter the world layer runs on every load
    json world = json::parse(R"({ "name": "room", "actors": [ { "name": "Table" } ] })");
    Worlds::ApplyWorldJsonFilters("room.world", world);
    ASSERT_EQ(world["actors"].size(), 2u);
    EXPECT_EQ(world["actors"][1]["name"], "Banner");
    json other = json::parse(R"({ "name": "other", "actors": [] })");
    Worlds::ApplyWorldJsonFilters("other.world", other);
    EXPECT_TRUE(other["actors"].empty());

    // and goes away with the pack
    ASSERT_TRUE(pm().Disable("pack.arena"));
    EXPECT_TRUE(pm().DeclaredComponents().empty());
    json again = json::parse(R"({ "name": "room", "actors": [ { "name": "Table" } ] })");
    Worlds::ApplyWorldJsonFilters("room.world", again);
    EXPECT_EQ(again["actors"].size(), 1u);
}

TEST_F(PluginManagerTest, AnInvalidDatapackDoesNotLoad)
{
    TempDir dir;
    dir.Write("plugins/bad/shard-plugin.json", PluginTest::DatapackManifest("pack.bad"));
    dir.Write("plugins/bad/content/tables/broken.json", "{ not json");
    config.searchPaths.push_back(dir.Sub("plugins"));
    ASSERT_TRUE(Start());
    EXPECT_EQ(State("pack.bad"), PluginState::Failed);
    EXPECT_NE(Rec("pack.bad")->error.find("broken.json"), std::string::npos);
    EXPECT_FALSE(pm().Content().Exists("mod://pack.bad/tables/broken.json"));
}

TEST_F(PluginManagerTest, TwoModulesProvidingTheSameFileAreReportedAndTheFirstIsKept)
{
    TempDir dir;
    dir.Write("plugins/a/shard-plugin.json", PluginTest::DatapackManifest("pack.a"));
    dir.Write("plugins/a/content/materials/x.mat", "a");
    dir.Write("plugins/b/shard-plugin.json", PluginTest::DatapackManifest("pack.b", "1.0.0", json{ { "loadAfter", json::array({ "pack.a" }) } }));
    dir.Write("plugins/b/content/materials/x.mat", "b");
    config.searchPaths.push_back(dir.Sub("plugins"));
    ASSERT_TRUE(Start());
    EXPECT_EQ(pm().Content().FindConflicts().size(), 1u);
    EXPECT_EQ(pm().Content().ResolveLogical("materials/x.mat")->pluginId, "pack.a");
}

// ---- Plugin sets, lockfile, safe mode ----

TEST_F(PluginManagerTest, TheSimulationSetHoldsOnlyWhatChangesTheSimulation)
{
    AddScripted(ModuleManifest("changes.it"));
    AddScripted(PluginManifestText("cosmetic"));
    ASSERT_TRUE(Start());
    BringUpEngine();
    const auto set = pm().SimulationPluginSet();
    ASSERT_EQ(set.size(), 1u);
    EXPECT_EQ(set[0].id, "changes.it");

    const uint64_t hash = pm().SimulationPluginHash();
    EXPECT_NE(hash, 0u);
    ASSERT_TRUE(pm().Disable("changes.it"));
    EXPECT_TRUE(pm().SimulationPluginSet().empty());
}

TEST_F(PluginManagerTest, ContentChangesTheSetHash)
{
    TempDir dir;
    dir.Write("plugins/pack/shard-plugin.json", PluginTest::DatapackManifest("pack.h"));
    dir.Write("plugins/pack/content/tables/t.json", R"({ "a": 1 })");
    config.searchPaths.push_back(dir.Sub("plugins"));
    ASSERT_TRUE(Start());
    const uint64_t first = pm().SimulationPluginHash();
    pm().ResetForTests();
    dir.Write("plugins/pack/content/tables/t.json", R"({ "a": 2 })");
    ASSERT_TRUE(Start());
    EXPECT_NE(pm().SimulationPluginHash(), first);
    pm().ResetForTests();
    dir.Write("plugins/pack/content/tables/t.json", R"({ "a": 1 })");
    ASSERT_TRUE(Start());
    EXPECT_EQ(pm().SimulationPluginHash(), first);
}

TEST(PluginSets, CompareJsonAndLinesRoundTrip)
{
    std::vector<PluginSetEntry> local = { { "a", "1.0.0", 11 }, { "b", "2.0.0", 22 }, { "c", "1.0.0", 33 } };
    std::vector<PluginSetEntry> remote = { { "a", "1.0.0", 11 }, { "b", "2.1.0", 22 }, { "c", "1.0.0", 99 }, { "d", "1.0.0", 1 } };
    const PluginSetDiff diff = ComparePluginSets(local, remote);
    EXPECT_FALSE(diff.Same());
    EXPECT_EQ(diff.missing, std::vector<std::string>{ "d 1.0.0" });
    EXPECT_TRUE(diff.extra.empty());
    EXPECT_EQ(diff.different.size(), 2u);
    EXPECT_TRUE(ComparePluginSets(local, local).Same());
    EXPECT_NE(diff.Describe().find("d 1.0.0"), std::string::npos);

    const auto viaJson = PluginSetFromJson(PluginSetToJson(local));
    EXPECT_TRUE(ComparePluginSets(local, viaJson).Same());
    const auto viaLines = PluginSetFromLines(PluginSetToLines(local));
    EXPECT_TRUE(ComparePluginSets(local, viaLines).Same());
    EXPECT_EQ(HashPluginSet(local), HashPluginSet({ local[2], local[0], local[1] }));          // the order of the list does not matter
    EXPECT_NE(HashPluginSet(local), HashPluginSet(remote));
}

TEST_F(PluginManagerTest, TheLockfileIsWrittenThenVerified)
{
    TempDir dir;
    dir.Write("plugins/pack/shard-plugin.json", PluginTest::DatapackManifest("pack.l", "1.2.0"));
    dir.Write("plugins/pack/content/tables/t.json", "{}");
    dir.Write("proj/project.json", R"({ "pluginsFolder": "../plugins" })");
    config.projectFile = dir.Sub("proj/project.json");
    ASSERT_TRUE(Start());
    ASSERT_FALSE(dir.Read("proj/plugins.lock").empty());
    const json lock = json::parse(dir.Read("proj/plugins.lock"));
    ASSERT_EQ(lock["plugins"].size(), 1u);
    EXPECT_EQ(lock["plugins"][0]["id"], "pack.l");
    EXPECT_EQ(lock["plugins"][0]["version"], "1.2.0");
    EXPECT_TRUE(pm().LockfileProblems().empty());

    // the same plugins : fine
    pm().ResetForTests();
    ASSERT_TRUE(Start());
    EXPECT_TRUE(pm().LockfileProblems().empty());

    // other content : reported, and an error when the lock is strict
    pm().ResetForTests();
    dir.Write("plugins/pack/content/tables/t.json", R"({ "changed": true })");
    ASSERT_TRUE(Start());
    ASSERT_EQ(pm().LockfileProblems().size(), 1u);
    EXPECT_NE(pm().LockfileProblems()[0].find("pack.l"), std::string::npos);

    pm().ResetForTests();
    config.strictLock = true;
    std::string error;
    EXPECT_FALSE(Start(&error));
    EXPECT_NE(error.find("plugins.lock"), std::string::npos);

    // a new version
    pm().ResetForTests();
    config.strictLock = false;
    dir.Write("plugins/pack/shard-plugin.json", PluginTest::DatapackManifest("pack.l", "1.3.0"));
    ASSERT_TRUE(Start());
    ASSERT_FALSE(pm().LockfileProblems().empty());
    EXPECT_NE(pm().LockfileProblems()[0].find("1.2.0"), std::string::npos);
}

TEST_F(PluginManagerTest, AReadOnlyRunLeavesNoLockfile)
{
    TempDir dir;
    dir.Write("plugins/pack/shard-plugin.json", PluginTest::DatapackManifest("pack.ro"));
    dir.Write("proj/project.json", R"({ "pluginsFolder": "../plugins" })");
    config.projectFile = dir.Sub("proj/project.json");
    config.readOnly = true;
    ASSERT_TRUE(Start());
    EXPECT_TRUE(dir.Read("proj/plugins.lock").empty());
}

TEST_F(PluginManagerTest, APluginTheLastRunCrashedInSitsThisOneOut)
{
    TempDir dir;
    dir.Write("proj/project.json", "{}");
    dir.Write("proj/.cache/plugin_safe_mode.json", R"({ "suspect": "crashy" })");
    config.projectFile = dir.Sub("proj/project.json");
    config.readOnly = false;
    AddScripted(ModuleManifest("crashy"));
    AddScripted(ModuleManifest("innocent"));
    ASSERT_TRUE(Start());
    EXPECT_EQ(Rec("crashy"), nullptr);
    EXPECT_NE(Rec("innocent"), nullptr);
    EXPECT_FALSE(Logged("load crashy"));
    // the marker is consumed : the next run tries it again
    EXPECT_TRUE(PluginManager::ReadSafeModeSuspect(dir.Sub("proj/.cache")).empty());
}

TEST_F(PluginManagerTest, IgnoreSafeModeLoadsItAnyway)
{
    TempDir dir;
    dir.Write("proj/project.json", "{}");
    dir.Write("proj/.cache/plugin_safe_mode.json", R"({ "suspect": "crashy" })");
    config.projectFile = dir.Sub("proj/project.json");
    config.ignoreSafeMode = true;
    AddScripted(ModuleManifest("crashy"));
    ASSERT_TRUE(Start());
    EXPECT_NE(Rec("crashy"), nullptr);
}

// ---- Hosts: server, tool, editor ----

TEST_F(PluginManagerTest, ServerPluginsAnswerConnectionsAndTicks)
{
    pm().AddStaticPlugin(PluginManifestText("srv"), &MakeServer);
    ASSERT_TRUE(Start());
    BringUpEngine();
    pm().NotifyServerStart(true);
    EXPECT_TRUE(Logged("server-start dedicated"));

    ClientInfo ok; ok.name = "alice";
    ConnectDecision d1;
    pm().NotifyClientConnect(ok, d1);
    EXPECT_TRUE(d1.allowed);

    ClientInfo banned; banned.name = "banned";
    ConnectDecision d2;
    pm().NotifyClientConnect(banned, d2);
    EXPECT_FALSE(d2.allowed);
    EXPECT_EQ(d2.reason, "banned player");

    pm().NotifyServerTick(5);
    EXPECT_TRUE(Logged("tick 5"));
}

TEST_F(PluginManagerTest, AHookCanRefuseAConnection)
{
    Scripts()["door"].onLoad = [](PluginContext& ctx) {
        ctx.Hooks().On<Hooks::PlayerConnecting>([](Hooks::PlayerConnecting& h) { if (h.address == "6.6.6.6") h.Cancel("blacklisted"); });
    };
    AddScripted(ModuleManifest("door"));
    ASSERT_TRUE(Start());
    BringUpEngine();
    ClientInfo c; c.name = "x"; c.address = "6.6.6.6";
    ConnectDecision d;
    pm().NotifyClientConnect(c, d);
    EXPECT_FALSE(d.allowed);
    EXPECT_EQ(d.reason, "blacklisted");
}

TEST_F(PluginManagerTest, AClientWithOtherPluginsIsRefused)
{
    AddScripted(ModuleManifest("shared.mod"));
    ASSERT_TRUE(Start());
    BringUpEngine();
    const json same = PluginSetToJson(pm().SimulationPluginSet());
    ConnectDecision ok;
    pm().CheckClientPlugins(same, ok);
    EXPECT_TRUE(ok.allowed);

    ConnectDecision missing;
    pm().CheckClientPlugins(json::array(), missing);
    EXPECT_FALSE(missing.allowed);
    EXPECT_NE(missing.reason.find("shared.mod"), std::string::npos);

    json other = same;
    other[0]["version"] = "9.9.9";
    ConnectDecision different;
    pm().CheckClientPlugins(other, different);
    EXPECT_FALSE(different.allowed);
}

TEST_F(PluginManagerTest, EditorPluginsAddPanelsAndHearTheSelection)
{
    config.host = Host::Editor;
    pm().AddStaticPlugin(PluginManifestText("ed"), &MakeEditor);
    ASSERT_TRUE(Start());
    BringUpEngine();
    pm().NotifyEditorInit();
    EXPECT_TRUE(Logged("editor-init"));
    EXPECT_EQ(pm().Extensions().Get<EditorPanelItem>("editor.panel").size(), 1u);
    pm().NotifySelectionChanged({ "a", "b" });
    EXPECT_TRUE(Logged("selection 2"));
    pm().NotifyEditorShutdown();
    EXPECT_TRUE(Logged("editor-shutdown"));
    ASSERT_TRUE(pm().Disable("ed"));
    EXPECT_TRUE(pm().Extensions().Get<EditorPanelItem>("editor.panel").empty());
}

TEST_F(PluginManagerTest, EditorPluginsAreLeftOutOfThePlayer)
{
    config.host = Host::Player;
    pm().AddStaticPlugin(PluginManifestText("ed", [](json& j) { j["hosts"] = json::array({ "editor" }); }), &MakeEditor);
    ASSERT_TRUE(Start());
    EXPECT_EQ(Rec("ed"), nullptr);
}

TEST_F(PluginManagerTest, ToolPluginsCanRefuseATask)
{
    config.host = Host::Tool;
    pm().AddStaticPlugin(PluginManifestText("tooly"), &MakeTool);
    ASSERT_TRUE(Start());
    BringUpEngine();
    std::string message;
    EXPECT_TRUE(pm().NotifyToolTask("validate", "p.json", message));
    EXPECT_FALSE(pm().NotifyToolTask("cook", "p.json", message));
    EXPECT_EQ(message, "no cooking today");
}

// ---- Modules follow the worlds ----

TEST_F(PluginManagerTest, ModulesHearTheLifeOfAWorld)
{
    AddScripted(ModuleManifest("watcher"));
    ASSERT_TRUE(Start());
    BringUpEngine();
    {
        Worlds::World world("w", Filesystem::Path("w.world"));
        world.OnLoad();
        EXPECT_TRUE(Logged("world-loaded watcher"));
        world.Stop();
        EXPECT_TRUE(Logged("play-stop watcher"));
        world.Unload();
        EXPECT_TRUE(Logged("world-unloaded watcher"));
    }
}

TEST_F(PluginManagerTest, ADisabledModuleHearsNothingMore)
{
    AddScripted(ModuleManifest("watcher"));
    ASSERT_TRUE(Start());
    BringUpEngine();
    ASSERT_TRUE(pm().Disable("watcher"));
    Log().clear();
    Worlds::World world("w", Filesystem::Path("w.world"));
    world.OnLoad();
    EXPECT_FALSE(Logged("world-loaded watcher"));
}

// ---- Tags ----

TEST_F(PluginManagerTest, APluginsTagsLiveAsLongAsItRuns)
{
    Core::TagRegistry::Get().Clear();
    AddScripted(ModuleManifest("weather", [](json& j) { j["tags"] = json::parse(R"({ "Weather.Rain": {} })"); }));
    ASSERT_TRUE(Start());
    EXPECT_TRUE(Core::Tag::Request("Weather.Rain").IsValid());
    EXPECT_EQ(Core::TagRegistry::Get().GetInfo(Core::Tag::Request("Weather.Rain"))->declarations[0].owner, "weather");

    ASSERT_TRUE(pm().Disable("weather"));
    EXPECT_FALSE(Core::TagRegistry::Get().Find("Weather.Rain").IsValid());
    Core::TagRegistry::Get().Clear();
}

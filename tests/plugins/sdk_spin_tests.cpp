#include <gtest/gtest.h>

#include <string>

#include "engine/core/config/cvar.hpp"
#include "engine/core/ecs/registry.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/world/actor.hpp"
#include "plugins/asset_test_context.hpp"
#include "support/test_engine_context.hpp"
#include "engine/world/components/registry/component_registry.hpp"

// The C++ SDK (sdk/include/shard/cpp) end to end : tests/plugin_fixtures/sdk_spin is a C++17 plugin written against the SDK alone, loaded
// through the C ABI like the plugins in C.

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;

namespace {
    const char* kId = "shard.test.spin";

    struct Spin {           // the fixture's struct
        float speed;
        float angle;
        float axis[3];
        int32_t turns;
    };

    class SdkSpinTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            PluginManager::Get().ResetForTests();
            config.host = Host::Headless;
            config.registerConsoleCommands = false;
            config.pluginPaths.push_back(std::string(SHARD_TEST_PLUGIN_DIR) + "/" + kId);
        }
        void TearDown() override { PluginManager::Get().ResetForTests(); }

        PluginManager& pm() { return PluginManager::Get(); }
        PluginRecord* Rec() { return pm().Find(kId); }
        template <typename Fn>
        Fn Export(const char* name) { return Rec()->library.Symbol<Fn>(name); }

        void Start(Core::IEngineContext* engine = nullptr)
        {
            std::string error;
            ASSERT_TRUE(pm().Startup(config, &error)) << error;
            ASSERT_NE(Rec(), nullptr);
            ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
            pm().OnEngineSystemsReady(engine, &scheduler, &events);
            pm().OnEngineInitialized();
            ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
        }
        void RunFrame(float dt)
        {
            Core::Ecs::Scheduler::FrameTiming timing;
            timing.deltaTime = dt;
            pm().BeginFrame(++frame);
            scheduler.RunFrame(registry, nullptr, timing);
            pm().EndFrame(frame);
        }
        Core::Ecs::ComponentId SpinId() { return Core::Ecs::ComponentRegistry::Global().Register("c:shard.test.spin.Spin", sizeof(Spin), alignof(Spin)); }

        PluginManagerConfig config;
        Core::Ecs::Scheduler scheduler;
        Events::EventDispatcher events;
        Core::Ecs::Registry registry;
        uint64_t frame = 0;
    };
}

TEST_F(SdkSpinTest, ThePluginOfTheDesignNoteLoadsAndSpins)
{
    Start();
    EXPECT_TRUE(Objects::Components::GetComponentRegistry().IsRegistered("shard.test.spin.Spin"));
    EXPECT_TRUE(scheduler.Has("shard.test.spin/rotate"));
    EXPECT_EQ(Core::Ecs::ComponentRegistry::Global().Info(SpinId()).size, sizeof(Spin));

    Spin start{ 2.0f, 0.0f, { 0.f, 1.f, 0.f }, 0 };
    const Core::Ecs::Entity e = registry.Create();
    ASSERT_TRUE(registry.AddRaw(e, SpinId(), &start));
    RunFrame(0.5f);
    EXPECT_FLOAT_EQ(static_cast<const Spin*>(registry.GetRaw(e, SpinId()))->angle, 1.0f);

    // The cvar the plugin reads in its system, changed by its own command
    ASSERT_TRUE(Core::CVarRegistry::Global().Execute("spin.scale 4").ok);
    EXPECT_EQ(Core::CVarRegistry::Global().Find("shard.test.spin.scale")->ToString(), "4");
    RunFrame(1.0f);                                                 // + 2 * 4 * 1 : a full turn and some
    const Spin* after = static_cast<const Spin*>(registry.GetRaw(e, SpinId()));
    EXPECT_EQ(after->turns, 1);
    EXPECT_NEAR(after->angle, 9.0f - 6.2831853f, 1e-4f);
    EXPECT_EQ(Export<int (*)()>("spin_turn_events")(), 1);          // emitted from the system, delivered at the end of the frame
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(SdkSpinTest, ARefusedRegistrationThrowsAShardError)
{
    Start();
    const std::string error = Export<const char* (*)()>("spin_last_error")();
    EXPECT_NE(error.find("NoSuchHook"), std::string::npos) << error;
    EXPECT_NE(error.find("not found"), std::string::npos) << error;
    EXPECT_FALSE(pm().BeforeSave("spin.world"));                    // its other hook was added
}

TEST_F(SdkSpinTest, AHotReloadKeepsTheScale)
{
    config.allowHotReload = true;
    Start();
    ASSERT_TRUE(Core::CVarRegistry::Global().Execute("spin.scale 3").ok);
    std::string error;
    ASSERT_TRUE(pm().Reload(kId, &error)) << error;
    ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
    EXPECT_EQ(Core::CVarRegistry::Global().Find("shard.test.spin.scale")->ToString(), "3");
}

TEST_F(SdkSpinTest, ItDescribesItsEventAndCountsItsEntitiesThroughTheExperimentalRegistry)
{
    config.registerConsoleCommands = true;
    std::string error;
    ASSERT_TRUE(pm().Startup(config, &error)) << error;
    Shard::Tests::TestEngineContext engine;
    pm().OnEngineSystemsReady(&engine, &scheduler, &events);
    pm().OnEngineInitialized();
    ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;

    // Events::Describe<Turned> : the fields SHARD_COMPONENT listed
    const Events::NamedEventLayout* layout = events.namedLayout("shard.test.spin.Turned");
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->fields.size(), 2u);
    EXPECT_EQ(layout->fields[1].name, "speed");

    // Shard::experimental::World over the engine's registry
    Spin spin{ 1.0f, 0.0f, { 0.f, 1.f, 0.f }, 0 };
    Core::Ecs::Registry& ecs = *engine.GetEcs();
    ASSERT_TRUE(ecs.AddRaw(ecs.Create(), SpinId(), &spin));
    ASSERT_TRUE(ecs.AddRaw(ecs.Create(), SpinId(), &spin));
    const Core::CommandResult count = Core::CVarRegistry::Global().Execute("spin.count");
    EXPECT_TRUE(count.ok) << count.output;
    EXPECT_EQ(count.output, "2");
}

// ABI 1.3 through the C++ SDK : Shard::Text, Shard::AssetId and SystemCtx::SetLocalTransform
TEST_F(SdkSpinTest, ATextAnAssetAndAMovingSystemWrittenWithTheSdk)
{
    PluginTest::AssetContext context;
    Core::SetEngine(&context);
    Start(&context);
    {
        struct SignBlock {          // the fixture's struct
            uint64_t text;
            int32_t icon;
            int32_t uses;
        };
        const auto signId = Core::Ecs::ComponentRegistry::Global().Register("c:shard.test.spin.Sign", sizeof(SignBlock), alignof(SignBlock));
        auto actor = Core::Object::CreateWithContext<Objects::Actor>(&context, std::string("Post"), &context);
        auto component = actor->AddComponentByName("shard.test.spin.Sign");
        ASSERT_NE(component, nullptr);
        component->Deserialize(nlohmann::json::parse(R"({"active":true,"text":"stop","icon":4,"uses":0})"));

        Core::Ecs::Registry& ecs = *context.GetEcs();
        Core::Ecs::Scheduler::FrameTiming timing;
        timing.deltaTime = 0.1f;
        scheduler.RunFrame(ecs, nullptr, timing);
        scheduler.RunFrame(ecs, nullptr, timing);

        const auto* block = static_cast<const SignBlock*>(ecs.GetRaw(actor->GetEntity(), signId));
        ASSERT_NE(block, nullptr);
        EXPECT_EQ(block->icon, 4);
        EXPECT_EQ(block->uses, 2);
        std::string text;
        ASSERT_TRUE(CAbi::BlobBytes(Rec()->owner, block->text, text));
        EXPECT_EQ(text, "stop**");
        EXPECT_FLOAT_EQ(actor->transform->GetPosition().x, 2.0f);
        EXPECT_FLOAT_EQ(actor->transform->GetScale().z, 3.0f);
        EXPECT_EQ(component->Serialize()["text"].get<std::string>(), "stop**");
        EXPECT_EQ(Rec()->errorCount, 0u);
    }
    Core::SetEngine(nullptr);
}

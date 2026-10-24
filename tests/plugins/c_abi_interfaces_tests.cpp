#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <shard/shard.h>

#include "support/test_engine_context.hpp"

#include "engine/assets/asset_importer.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/core/config/cvar.hpp"
#include "engine/core/ecs/registry.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/ecs_components.hpp"
#include "engine/world/components/registry/component_registry.hpp"
#include "plugins/asset_test_context.hpp"
#include "plugins/plugin_test_utils.hpp"

#include "config.h"

// The interfaces of the C ABI past the base (components, ECS, systems, events, hooks, services, extensions, content, state), through
// the C plugins of tests/plugin_fixtures/c_plugin and c_consumer, loaded for real.

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using nlohmann::json;
using PluginTest::TempDir;

namespace {
    const char* kId = "shard.test.cplugin";
    const char* kConsumer = "shard.test.cconsumer";

    std::string FixtureDir(const char* id) { return std::string(SHARD_TEST_PLUGIN_DIR) + "/" + id; }

    // The block the plugin's Counter type describes (tests/plugin_fixtures/c_plugin/c_plugin.c)
    struct Counter {
        float value;
        int32_t ticks;
        float rate;
        uint8_t enabled;
        uint8_t pad[3];
        float axis[3];
        uint32_t scratch;
    };

    class CAbiTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            PluginManager::Get().ResetForTests();
            config.host = Host::Headless;
            config.registerConsoleCommands = false;
            config.pluginPaths.push_back(FixtureDir(kId));
        }
        void TearDown() override { PluginManager::Get().ResetForTests(); }

        PluginManager& pm() { return PluginManager::Get(); }
        PluginRecord* Rec(const char* id = kId) { return pm().Find(id); }

        template <typename Fn>
        Fn Export(const char* name, const char* id = kId) { return Rec(id)->library.Symbol<Fn>(name); }
        int Int(const char* name, const char* id = kId) { return Export<int (*)()>(name, id)(); }

        void Start()
        {
            std::string error;
            ASSERT_TRUE(pm().Startup(config, &error)) << error;
            ASSERT_NE(Rec(), nullptr);
            ASSERT_EQ(Rec()->state, PluginState::Initialized) << Rec()->error;
            ASSERT_EQ(Int("c_plugin_setup_error"), 0);
        }
        void BringUpEngine()
        {
            pm().OnEngineSystemsReady(nullptr, &scheduler, &events);
            pm().OnEngineInitialized();
            ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
        }
        void RunFrame(Core::Ecs::Registry& registry, float dt = 0.5f)
        {
            Core::Ecs::Scheduler::FrameTiming timing;
            timing.deltaTime = dt;
            pm().BeginFrame(++frame);
            scheduler.RunFrame(registry, nullptr, timing);
            pm().EndFrame(frame);
        }
        Core::Ecs::ComponentId CounterId() { return Core::Ecs::ComponentId(Export<uint64_t (*)()>("c_plugin_counter_type")() - 1); }

        PluginManagerConfig config;
        Core::Ecs::Scheduler scheduler;
        Events::EventDispatcher events;
        uint64_t frame = 0;
    };
}

// ---- shard.components ----

TEST_F(CAbiTest, AComponentTypeIsAnActorComponentAndAnEcsComponent)
{
    Start();
    const std::string name = std::string(kId) + ".Counter";
    EXPECT_TRUE(Objects::Components::GetComponentRegistry().IsRegistered(name));
    const Core::Ecs::ComponentInfo& info = Core::Ecs::ComponentRegistry::Global().Info(CounterId());
    EXPECT_EQ(info.size, sizeof(Counter));
    EXPECT_EQ(info.name, "c:" + name);
    EXPECT_TRUE(Rec()->context->Systems().Has("tick"));         // no scheduler yet : the system waits for it

    // Another plugin reads the layout without the header
    uint32_t offset = 0, type = 0;
    auto describe = Export<int (*)(const char*, uint32_t*, uint32_t*)>("c_plugin_describe_field");
    EXPECT_EQ(describe("axis", &offset, &type), SHARD_OK);
    EXPECT_EQ(offset, offsetof(Counter, axis));
    EXPECT_EQ(type, SHARD_FIELD_VEC3);
    EXPECT_EQ(describe("nope", &offset, &type), SHARD_E_NOT_FOUND);

    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_FALSE(Objects::Components::GetComponentRegistry().IsRegistered(name));
    ASSERT_TRUE(pm().Enable(kId, &error)) << error;                  // registered again, under the same ECS id
    EXPECT_EQ(Int("c_plugin_setup_error"), 0);
    EXPECT_TRUE(Objects::Components::GetComponentRegistry().IsRegistered(name));
}

// ---- shard.systems, shard.ecs ----

TEST_F(CAbiTest, ACSystemRunsOverTheEntitiesThatHaveItsComponent)
{
    Start();
    BringUpEngine();
    Core::Ecs::Registry registry;
    Counter start{};
    start.rate = 4.0f;
    const Core::Ecs::Entity e = registry.Create();
    ASSERT_TRUE(registry.AddRaw(e, CounterId(), &start));
    registry.Create();                                              // no Counter : not visited

    RunFrame(registry, 0.5f);
    EXPECT_EQ(Int("c_plugin_system_runs"), 1);
    EXPECT_EQ(Export<uint64_t (*)()>("c_plugin_chunk_entities")(), 1u);
    const auto* after = static_cast<const Counter*>(registry.GetRaw(e, CounterId()));
    ASSERT_NE(after, nullptr);
    EXPECT_FLOAT_EQ(after->value, 2.0f);
    EXPECT_EQ(after->ticks, 1);

    // The entity it spawned through the commands exists once the phase is over, with the value it gave
    EXPECT_EQ(registry.CountRaw({ CounterId() }, {}), 2u);
    RunFrame(registry, 0.5f);
    EXPECT_EQ(Export<uint64_t (*)()>("c_plugin_chunk_entities")(), 2u);

    EXPECT_EQ(Int("c_plugin_undeclared_get"), SHARD_E_INVALID_ARG);   // it did not declare shard.LocalTransform
    EXPECT_EQ(Int("c_plugin_wrong_thread"), SHARD_E_WRONG_THREAD);     // a "thread : main" function from a system
    EXPECT_EQ(Int("c_plugin_frames_begun"), 2);
    EXPECT_EQ(Int("c_plugin_frames_ended"), 2);
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(CAbiTest, TheSystemLeavesWithThePlugin)
{
    Start();
    BringUpEngine();
    const std::string full = std::string(kId) + "/tick";
    EXPECT_TRUE(scheduler.Has(full));
    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_FALSE(scheduler.Has(full));
}

// What the plugin's systems wrote is what the actor's component saves ; loading the saved component puts the values back on the entity

TEST_F(CAbiTest, AnActorComponentMirrorsItsEntityAndSavesWhatTheSystemsWrote)
{
    Start();
    BringUpEngine();
    Shard::Tests::TestEngineContext context;
    Core::SetEngine(&context);
    const std::string type = std::string(kId) + ".Counter";
    {
        auto actor = Core::Object::CreateWithContext<Objects::Actor>(&context, std::string("Spinner"), &context);
        auto component = actor->AddComponentByName(type);
        ASSERT_NE(component, nullptr);
        Core::Ecs::Registry& ecs = *context.GetEcs();
        const auto* onEntity = static_cast<const Counter*>(ecs.GetRaw(actor->GetEntity(), CounterId()));
        ASSERT_NE(onEntity, nullptr) << "attaching the component put its values on the actor's entity";
        EXPECT_FLOAT_EQ(onEntity->rate, 2.0f);                        // the defaults the plugin gave
        EXPECT_EQ(onEntity->enabled, 1);

        RunFrame(ecs, 1.0f);
        RunFrame(ecs, 1.0f);

        const ordered_json saved = component->Serialize();
        EXPECT_EQ(saved["type"], type);
        EXPECT_FLOAT_EQ(saved["value"].get<float>(), 4.0f);
        EXPECT_EQ(saved["ticks"].get<int>(), 2);
        EXPECT_FLOAT_EQ(saved["axis"]["y"].get<float>(), 1.0f);
        EXPECT_FALSE(saved.contains("scratch"));                       // private : not saved

        // Loaded on another actor : the values reach its entity
        auto other = Core::Object::CreateWithContext<Objects::Actor>(&context, std::string("Copy"), &context);
        auto loaded = other->AddComponentByName(type);
        ASSERT_NE(loaded, nullptr);
        loaded->Deserialize(json::parse(saved.dump()));
        const auto* copy = static_cast<const Counter*>(ecs.GetRaw(other->GetEntity(), CounterId()));
        ASSERT_NE(copy, nullptr);
        EXPECT_FLOAT_EQ(copy->value, 4.0f);
        EXPECT_EQ(copy->ticks, 2);
        EXPECT_EQ(copy->scratch, 0u);

        // A deactivated component leaves the entity (the systems skip it), and comes back with its values
        loaded->DeActivate();
        EXPECT_EQ(ecs.GetRaw(other->GetEntity(), CounterId()), nullptr);
        loaded->Activate();
        ASSERT_NE(ecs.GetRaw(other->GetEntity(), CounterId()), nullptr);
        EXPECT_EQ(static_cast<const Counter*>(ecs.GetRaw(other->GetEntity(), CounterId()))->ticks, 2);

        // A copy of the actor (play mode) carries the values to its own entity
        auto clone = other->Clone();
        ASSERT_NE(clone, nullptr);
        const auto* cloned = static_cast<const Counter*>(ecs.GetRaw(clone->GetEntity(), CounterId()));
        ASSERT_NE(cloned, nullptr);
        EXPECT_EQ(cloned->ticks, 2);
    }
    Core::SetEngine(nullptr);
}

// ---- shard.events ----

TEST_F(CAbiTest, EventsASystemEmitsArriveAtTheEndOfTheFrame)
{
    Start();
    BringUpEngine();
    Core::Ecs::Registry registry;
    int heard = 0;
    int32_t ticks = 0;
    events.subscribeNamed("shard.test.cplugin.Ticked", [&](const Events::NamedEvent& e) {
        ++heard;
        ASSERT_EQ(e.size, sizeof(int32_t));
        std::memcpy(&ticks, e.payload, sizeof(ticks));
    });

    RunFrame(registry);
    RunFrame(registry);
    EXPECT_EQ(heard, 2);                        // C++ code hears the C plugin's events
    EXPECT_EQ(ticks, 2);
    EXPECT_EQ(Int("c_plugin_ticks_received"), 2);
    EXPECT_EQ(Int("c_plugin_last_ticks"), 2);

    // An event emitted from the main thread reaches the C subscriber at once
    const int32_t value = 99;
    events.emitNamed("shard.test.cplugin.Ticked", &value, sizeof(value));
    EXPECT_EQ(Int("c_plugin_last_ticks"), 99);

    EXPECT_EQ(Int("c_plugin_unsubscribe"), SHARD_OK);
    EXPECT_EQ(Int("c_plugin_unsubscribe"), SHARD_E_NOT_FOUND);
    RunFrame(registry);
    EXPECT_EQ(Int("c_plugin_ticks_received"), 3);   // the 99 above ; nothing after the unsubscribe
    EXPECT_EQ(heard, 4);
}

// ---- shard.hooks ----

TEST_F(CAbiTest, AHookWrittenInCCanCancel)
{
    Start();
    BringUpEngine();
    EXPECT_TRUE(pm().BeforeSave("fine.world"));
    EXPECT_FALSE(pm().BeforeSave("forbidden.world"));
    EXPECT_EQ(Int("c_plugin_hook_calls"), 2);

    Hooks::PreSave hook("forbidden.world");
    pm().Hooks().Emit(hook);
    EXPECT_TRUE(hook.cancelled);
    EXPECT_EQ(hook.reason, "cplugin says no");
    EXPECT_EQ(hook.cancelledBy, Rec()->owner);

    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_TRUE(pm().BeforeSave("forbidden.world"));               // gone with the plugin
}

// ---- shard.services ----

TEST_F(CAbiTest, OneCPluginUsesAServiceAnotherProvidesAndLosesItWhenItGoes)
{
    config.pluginPaths.push_back(FixtureDir(kConsumer));
    Start();
    ASSERT_NE(Rec(kConsumer), nullptr);
    ASSERT_EQ(Rec(kConsumer)->state, PluginState::Initialized) << Rec(kConsumer)->error;
    EXPECT_EQ(Int("c_consumer_get_result", kConsumer), SHARD_OK);

    auto add = Export<int (*)(int, int, int*)>("c_consumer_add", kConsumer);
    int sum = 0;
    ASSERT_EQ(add(2, 3, &sum), SHARD_OK);
    EXPECT_EQ(sum, 1005);                                           // the provider's table, with its `self`
    EXPECT_EQ(Export<unsigned (*)()>("c_consumer_version", kConsumer)(), 102u);

    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    ASSERT_EQ(Rec(kConsumer)->state, PluginState::Initialized);    // no declared dependency : it keeps running
    EXPECT_EQ(add(2, 3, &sum), SHARD_E_NOT_FOUND);                  // its ref is empty, not dangling
}

// ---- shard.extensions ----

TEST_F(CAbiTest, AValidatorWrittenInCRunsInTheAssetValidator)
{
    Start();
    BringUpEngine();
    const std::vector<std::string> problems = pm().RunToolValidators("project.json");
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0], "cplugin.check : checked project.json");
}

TEST_F(CAbiTest, AnImporterAndAnAssetTypeWrittenInC)
{
    Start();
    BringUpEngine();
    TempDir dir;
    auto importers = pm().Extensions().Get<Filesystem::AssetImporterExtension>("asset.importer");
    ASSERT_EQ(importers.size(), 1u);
    EXPECT_EQ(importers[0]->extensions, std::vector<std::string>{ ".cpin" });
    std::filesystem::create_directories(dir.Sub("out"));
    const Filesystem::ImportResult imported = importers[0]->import("source.cpin", dir.Sub("out"));
    EXPECT_TRUE(imported.ok) << imported.message;
    EXPECT_EQ(imported.message, "imported");
    ASSERT_EQ(imported.outputs.size(), 1u);
    EXPECT_EQ(dir.Read("out/imported.txt"), "from source.cpin");

    auto types = pm().Extensions().Get<Core::Resources::AssetTypeExtension>("asset.type");
    ASSERT_EQ(types.size(), 1u);
    Filesystem::AssetInfos infos;
    const uint64_t before = Rec()->context->Memory().Used();
    std::shared_ptr<void> resource = types[0]->info.load("data/a.cpasset", infos);
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(*static_cast<int32_t*>(resource.get()), 77);
    EXPECT_GT(Rec()->context->Memory().Used(), before);             // the plugin allocated it from the host
    resource.reset();                                               // the cache let it go : back to the plugin
    EXPECT_EQ(Int("c_plugin_unloaded_assets"), 1);
    EXPECT_EQ(Rec()->context->Memory().Used(), before);

    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_TRUE(pm().Extensions().Get<Filesystem::AssetImporterExtension>("asset.importer").empty());
}

TEST_F(CAbiTest, AToolTaskCanBeRefusedFromC)
{
    Start();
    BringUpEngine();
    std::string message;
    EXPECT_FALSE(pm().NotifyToolTask("cook", "project.json", message));
    EXPECT_EQ(message, "cplugin refuses cooking");
    message.clear();
    EXPECT_TRUE(pm().NotifyToolTask("bake", "project.json", message));
}

// ---- shard.content ----

TEST_F(CAbiTest, ThePluginReadsItsContentThroughModUris)
{
    Start();
    EXPECT_EQ(Int("c_plugin_content_mounted"), 1);
    EXPECT_STREQ(Export<const char* (*)()>("c_plugin_content_text")(), "hello from content");
    EXPECT_EQ(Int("c_plugin_content_files"), 2);
    EXPECT_EQ(Rec()->context->Memory().Used(), 0u);                 // the file's memory was given back
}

// ---- shard.state ----

TEST_F(CAbiTest, AHotReloadKeepsWhatThePluginSaved)
{
    config.allowHotReload = true;
    Start();
    BringUpEngine();
    std::string error;
    ASSERT_TRUE(Core::CVarRegistry::Global().Execute("cplugin.persist 42").ok);
    ASSERT_TRUE(pm().Reload(kId, &error)) << error;
    ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
    EXPECT_EQ(Int("c_plugin_restored"), 42);                        // read by the new copy of the library
    EXPECT_EQ(Rec()->reloadCount, 1u);
}

// ---- The tables ----

TEST_F(CAbiTest, EveryInterfaceOfTheV1IsOffered)
{
    Start();
    const char* ids[] = { SHARD_IFACE_PLUGIN, SHARD_IFACE_CVARS, SHARD_IFACE_COMMANDS, SHARD_IFACE_COMPONENTS, SHARD_IFACE_ECS, SHARD_IFACE_SYSTEMS,
                          SHARD_IFACE_EVENTS, SHARD_IFACE_HOOKS, SHARD_IFACE_SERVICES, SHARD_IFACE_EXTENSIONS, SHARD_IFACE_CONTENT, SHARD_IFACE_STATE };
    for (const char* id : ids) {
        const void* table = nullptr;
        const ShardHostApi* api = GetCHostApi();
        EXPECT_EQ(api->query_interface(ShardStr{ id, std::strlen(id) }, 1, 0, &table), SHARD_OK) << id;
        ASSERT_NE(table, nullptr) << id;
        EXPECT_GE(*static_cast<const uint32_t*>(table), sizeof(uint32_t) * 2) << id;
        EXPECT_EQ(api->query_interface(ShardStr{ id, std::strlen(id) }, 2, 0, &table), SHARD_E_NOT_FOUND) << id;
    }
}

// ---- Crash attribution ----

namespace {
    std::vector<uint32_t> gSeenActive;
    void RecordActive() { gSeenActive.push_back(PluginManager::Get().ActivePlugin()); }
}

TEST_F(CAbiTest, EveryCallbackIntoThePluginNamesItForACrashReport)
{
    Start();
    BringUpEngine();
    gSeenActive.clear();
    Export<void (*)(void (*)())>("c_plugin_set_probe")(&RecordActive);

    ASSERT_TRUE(Core::CVarRegistry::Global().Execute("cplugin.echo x").ok);                 // a command
    const int32_t ticks = 1;
    events.emitNamed("shard.test.cplugin.Ticked", &ticks, sizeof(ticks));                   // an event
    pm().BeforeSave("fine.world");                                                          // a hook
    pm().RunToolValidators("project.json");                                                 // a validator
    TempDir dir;
    std::filesystem::create_directories(dir.Sub("out"));
    pm().Extensions().Get<Filesystem::AssetImporterExtension>("asset.importer")[0]->import("a.cpin", dir.Sub("out"));    // an importer
    Filesystem::AssetInfos infos;
    pm().Extensions().Get<Core::Resources::AssetTypeExtension>("asset.type")[0]->info.load("a.cpasset", infos).reset();   // load + unload
    Export<void (*)(void (*)())>("c_plugin_set_probe")(nullptr);

    ASSERT_EQ(gSeenActive.size(), 7u);
    for (uint32_t active : gSeenActive) EXPECT_EQ(active, Rec()->owner);
    EXPECT_EQ(pm().ActivePlugin(), 0u);                                     // and nobody once they returned
}

TEST_F(CAbiTest, TheHostSaysWhichEngineItIs)
{
    const ShardHostApi* host = GetCHostApi();
    EXPECT_EQ(std::string(host->engineVersion.data, size_t(host->engineVersion.size)), VERSION);
}

// ---- ABI 1.2 : blobs (shard.components 1.1) ----

namespace {
    // The block of the plugin's Chunk type
    struct Chunk {
        uint32_t count;
        uint32_t pad;
        uint64_t voxels;
    };
}

TEST_F(CAbiTest, ABlobIsSavedWithTheWorldCopiedWithTheActorAndFreedWithTheComponent)
{
    Start();
    BringUpEngine();
    Shard::Tests::TestEngineContext context;
    Core::SetEngine(&context);
    {
        const std::string type = std::string(kId) + ".Chunk";
        const auto chunkId = Core::Ecs::ComponentId(Export<uint64_t (*)()>("c_plugin_chunk_type")() - 1);
        auto makeBlob = Export<uint64_t (*)(uint32_t)>("c_plugin_make_blob");
        auto checkBlob = Export<int (*)(uint64_t, uint32_t)>("c_plugin_check_blob");
        Core::Ecs::Registry& ecs = *context.GetEcs();
        const uint64_t memoryBefore = Rec()->context->Memory().Used();

        auto actor = Core::Object::CreateWithContext<Objects::Actor>(&context, std::string("Terrain"), &context);
        auto component = actor->AddComponentByName(type);
        ASSERT_NE(component, nullptr);
        auto* block = static_cast<Chunk*>(ecs.GetRaw(actor->GetEntity(), chunkId));
        ASSERT_NE(block, nullptr);
        EXPECT_EQ(block->voxels, 0u);                                   // a blob field starts empty

        // What a system would do : a blob of 1000 bytes into the block
        const uint64_t blob = makeBlob(1000);
        ASSERT_NE(blob, 0u);
        block->voxels = blob;
        block->count = 7;
        EXPECT_GE(Rec()->context->Memory().Used(), memoryBefore + 1000);   // counted against the plugin's budget

        const ordered_json saved = component->Serialize();
        EXPECT_EQ(saved["count"].get<uint32_t>(), 7u);
        ASSERT_TRUE(saved["voxels"].is_string());
        EXPECT_EQ(saved["voxels"].get<std::string>().size(), 1336u);     // base64 of 1000 bytes

        // Loaded on another actor : a blob of its own, with the same bytes
        auto other = Core::Object::CreateWithContext<Objects::Actor>(&context, std::string("Loaded"), &context);
        auto loaded = other->AddComponentByName(type);
        loaded->Deserialize(json::parse(saved.dump()));
        const auto* loadedBlock = static_cast<const Chunk*>(ecs.GetRaw(other->GetEntity(), chunkId));
        ASSERT_NE(loadedBlock, nullptr);
        EXPECT_NE(loadedBlock->voxels, blob);
        EXPECT_EQ(checkBlob(loadedBlock->voxels, 1000), SHARD_OK);
        EXPECT_EQ(loadedBlock->count, 7u);

        // A copy of the actor (play mode) : another blob again
        auto clone = other->Clone();
        const auto* cloneBlock = static_cast<const Chunk*>(ecs.GetRaw(clone->GetEntity(), chunkId));
        ASSERT_NE(cloneBlock, nullptr);
        EXPECT_NE(cloneBlock->voxels, loadedBlock->voxels);
        EXPECT_EQ(checkBlob(cloneBlock->voxels, 1000), SHARD_OK);

        // Removing the component frees the blob it owned
        actor->RemoveComponent(component);
        EXPECT_EQ(checkBlob(blob, 1000), SHARD_E_STALE_HANDLE);

        // Resize keeps the first bytes ; free makes the handle stale
        const uint64_t loose = makeBlob(10);
        EXPECT_EQ(Export<int (*)(uint64_t, uint32_t)>("c_plugin_resize_and_free")(loose, 4), SHARD_OK);
        EXPECT_EQ(checkBlob(loose, 4), SHARD_E_STALE_HANDLE);

        // The blobs go with the plugin : nothing of its budget is left when its context goes
        const uint64_t kept = loadedBlock->voxels;
        std::string error;
        ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
        ASSERT_TRUE(pm().Enable(kId, &error)) << error;
        EXPECT_EQ(checkBlob(kept, 1000), SHARD_E_STALE_HANDLE);
    }
    Core::SetEngine(nullptr);
}

// ---- shard.experimental.* ----

TEST_F(CAbiTest, TheEngineRegistryFromTheMainThreadIsExperimental)
{
    Start();
    Shard::Tests::TestEngineContext engine;
    pm().OnEngineSystemsReady(&engine, &scheduler, &events);
    pm().OnEngineInitialized();
    ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;

    Core::Ecs::Registry& ecs = *engine.GetEcs();
    Counter counter{};
    ASSERT_TRUE(ecs.AddRaw(ecs.Create(), CounterId(), &counter));
    uint64_t count = 0;
    EXPECT_EQ(Export<int (*)(uint64_t*)>("c_plugin_world_count")(&count), SHARD_OK);
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(ecs.CountRaw({ CounterId() }, {}), 2u);                 // what it spawned, applied at close
    EXPECT_EQ(Core::CVarRegistry::Global().Find("shard.test.cplugin.counter")->ToString(), "3");     // main thread functions kept working

    // Only that revision : no promise between two of them
    const ShardHostApi* host = GetCHostApi();
    const void* table = nullptr;
    const ShardStr id{ SHARD_IFACE_EXPERIMENTAL_WORLD_ECS, std::strlen(SHARD_IFACE_EXPERIMENTAL_WORLD_ECS) };
    EXPECT_EQ(host->query_interface(id, 0, 1, &table), SHARD_OK);
    EXPECT_EQ(host->query_interface(id, 0, 0, &table), SHARD_E_NOT_FOUND);
    EXPECT_EQ(host->query_interface(id, 1, 1, &table), SHARD_E_NOT_FOUND);
}

TEST_F(CAbiTest, ADistributedPluginIsRefusedTheExperimentalInterfaces)
{
    using Status = SignatureInfo::Status;
    EXPECT_FALSE(MayUseExperimental(TrustTier::Native, Status::Valid));         // signed by a trusted publisher : distributed
    EXPECT_TRUE(MayUseExperimental(TrustTier::Native, Status::Unsigned));       // in development
    EXPECT_TRUE(MayUseExperimental(TrustTier::Native, Status::Untrusted));
    EXPECT_TRUE(MayUseExperimental(TrustTier::FirstParty, Status::Valid));      // the engine's own
}

// ---- shard.events 1.1 : described events ----

TEST_F(CAbiTest, AnEventThePluginDescribedIsShownWithItsFields)
{
    config.registerConsoleCommands = true;
    Start();
    BringUpEngine();
    const Events::NamedEventLayout* layout = events.namedLayout("shard.test.cplugin.Ticked");
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->size, 4u);
    ASSERT_EQ(layout->fields.size(), 1u);
    EXPECT_EQ(layout->fields[0].name, "ticks");
    const int32_t ticks = 5;
    EXPECT_EQ(Events::FormatNamedEvent(*layout, &ticks, sizeof(ticks)), "{ ticks: 5 }");
    EXPECT_EQ(events.namedLayout("shard.test.cplugin.Broken"), nullptr);       // refused : a field past the payload

    auto& console = Core::CVarRegistry::Global();
    const Core::CommandResult list = console.Execute("event.list");
    EXPECT_TRUE(list.ok);
    EXPECT_NE(list.output.find("shard.test.cplugin.Ticked (4 bytes) : ticks@0"), std::string::npos) << list.output;
    EXPECT_TRUE(console.Execute("event.watch shard.test.cplugin.Ticked").ok);
    events.emitNamed("shard.test.cplugin.Ticked", &ticks, sizeof(ticks));     // logged, and still delivered to the plugin
    EXPECT_EQ(Int("c_plugin_last_ticks"), 5);
    EXPECT_TRUE(console.Execute("event.unwatch shard.test.cplugin.Ticked").ok);
    EXPECT_FALSE(console.Execute("event.unwatch shard.test.cplugin.Ticked").ok);

    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_EQ(events.namedLayout("shard.test.cplugin.Ticked"), nullptr);      // gone with the plugin
}

// ---- ABI 1.3 : text and assets in a component, moving an actor ----

namespace {
    // The block of the plugin's Label type
    struct Label {
        uint64_t name;
        int32_t icon;
        int32_t visits;
    };
}

TEST_F(CAbiTest, ATextAndAnAssetAreSavedShownAndOwnedByTheActorComponent)
{
    Start();
    BringUpEngine();
    PluginTest::AssetContext context;
    Core::SetEngine(&context);
    {
        const std::string type = std::string(kId) + ".Label";
        const auto labelId = Core::Ecs::ComponentId(Export<uint64_t (*)()>("c_plugin_label_type")() - 1);
        Core::Ecs::Registry& ecs = *context.GetEcs();
        auto textOf = [&](uint64_t blob) {
            std::string bytes;
            return CAbi::BlobBytes(Rec()->owner, blob, bytes) ? bytes : std::string("<stale>");
        };

        auto actor = Core::Object::CreateWithContext<Objects::Actor>(&context, std::string("Sign"), &context);
        auto component = actor->AddComponentByName(type);
        ASSERT_NE(component, nullptr);

        // The inspector sees the text as a string and the asset as an asset ; the blob handle is no field of its own
        const ClassDescriptor* descriptor = component->GetDescriptor();
        ASSERT_NE(descriptor, nullptr);
        EXPECT_EQ(descriptor->fields.size(), 3u);
        bool sawText = false, sawAsset = false;
        for (const FieldInfo* field : descriptor->fields) {
            if (std::string(field->name) == "name") sawText = field->type == TypeID::String;
            if (std::string(field->name) == "icon") sawAsset = field->type == TypeID::Asset;
        }
        EXPECT_TRUE(sawText);
        EXPECT_TRUE(sawAsset);

        // Loaded from a world file : the text is in a blob on the entity, the asset is the number
        component->Deserialize(json::parse(R"({"active":true,"name":"hello","icon":7,"visits":3})"));
        auto* block = static_cast<Label*>(ecs.GetRaw(actor->GetEntity(), labelId));
        ASSERT_NE(block, nullptr);
        EXPECT_EQ(block->icon, 7);
        EXPECT_EQ(block->visits, 3);
        ASSERT_NE(block->name, 0u);
        EXPECT_EQ(textOf(block->name), "hello");

        // Edited : the same blob holds the new text
        const uint64_t first = block->name;
        component->Deserialize(json::parse(R"({"active":true,"name":"edited a lot","icon":7,"visits":3})"));
        block = static_cast<Label*>(ecs.GetRaw(actor->GetEntity(), labelId));
        EXPECT_EQ(block->name, first);
        EXPECT_EQ(textOf(block->name), "edited a lot");

        // What the system wrote into the text is what is saved
        RunFrame(ecs);
        RunFrame(ecs);
        EXPECT_EQ(Int("c_plugin_label_result"), 0);
        const ordered_json saved = component->Serialize();
        EXPECT_EQ(saved["name"].get<std::string>(), "edited a lot!!");
        EXPECT_EQ(saved["visits"].get<int>(), 5);
        EXPECT_TRUE(saved.contains("icon"));                           // by name, which an empty database does not have

        // A copy of the actor (play mode) has a text of its own
        auto clone = actor->Clone();
        const auto* cloned = static_cast<const Label*>(ecs.GetRaw(clone->GetEntity(), labelId));
        ASSERT_NE(cloned, nullptr);
        EXPECT_NE(cloned->name, block->name);
        EXPECT_EQ(textOf(cloned->name), "edited a lot!!");
        EXPECT_EQ(cloned->icon, 7);

        // Removing the component frees its text
        const uint64_t owned = block->name;
        actor->RemoveComponent(component);
        EXPECT_EQ(textOf(owned), "<stale>");
    }
    Core::SetEngine(nullptr);
}

TEST_F(CAbiTest, ASystemMovesTheActorBehindAnEntityAtTheEndOfThePhase)
{
    Start();
    PluginTest::AssetContext context;
    Core::SetEngine(&context);
    pm().OnEngineSystemsReady(&context, &scheduler, &events);
    pm().OnEngineInitialized();
    ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
    {
        const std::string type = std::string(kId) + ".Label";
        const auto labelId = Core::Ecs::ComponentId(Export<uint64_t (*)()>("c_plugin_label_type")() - 1);
        Core::Ecs::Registry& ecs = *context.GetEcs();

        auto actor = Core::Object::CreateWithContext<Objects::Actor>(&context, std::string("Walker"), &context);
        ASSERT_NE(actor->AddComponentByName(type), nullptr);
        ASSERT_NE(actor->transform, nullptr);

        // An entity the plugin made itself has no actor : set_local_transform says so, nothing moves
        const Core::Ecs::Entity bare = ecs.Create();
        const Label none{};
        ASSERT_TRUE(ecs.AddRaw(bare, labelId, &none));

        RunFrame(ecs);
        EXPECT_FLOAT_EQ(actor->transform->GetPosition().x, 1.0f);            // the Transform took the pose...
        EXPECT_FLOAT_EQ(actor->transform->GetScale().y, 2.0f);
        const auto* mirror = ecs.TryGet<Worlds::LocalTransform>(actor->GetEntity());
        ASSERT_NE(mirror, nullptr);
        EXPECT_FLOAT_EQ(mirror->position.x, 1.0f);                           // ...and so did its entity
        EXPECT_FLOAT_EQ(mirror->scale.z, 2.0f);
        RunFrame(ecs);
        EXPECT_FLOAT_EQ(actor->transform->GetPosition().x, 2.0f);

        EXPECT_EQ(Int("c_plugin_label_result"), 0);
        EXPECT_EQ(Int("c_plugin_label_no_actor"), 2);                        // the bare entity, twice
        EXPECT_EQ(Int("c_plugin_undeclared_pose"), SHARD_E_INVALID_ARG);     // a system that did not declare the transform may not move anything
        EXPECT_EQ(Rec()->errorCount, 0u);

        // A deactivated actor ignores it
        actor->transform->DeActivate();
        RunFrame(ecs);
        EXPECT_FLOAT_EQ(actor->transform->GetPosition().x, 2.0f);
    }
    Core::SetEngine(nullptr);
}

TEST_F(CAbiTest, TheInterfacesOfEachRevisionAreOffered)
{
    Start();
    const ShardHostApi* api = GetCHostApi();
    const void* table = nullptr;
    auto ask = [&](const char* id, uint32_t minor) { return api->query_interface(ShardStr{ id, std::strlen(id) }, 1, minor, &table); };
    EXPECT_EQ(ask(SHARD_IFACE_COMPONENTS, 3), SHARD_OK);            // 1.5 : actor references and accessor strings (1.3 : text and assets)
    EXPECT_EQ(ask(SHARD_IFACE_COMPONENTS, 4), SHARD_E_NOT_FOUND);
    EXPECT_EQ(ask(SHARD_IFACE_ECS, 1), SHARD_OK);                   // 1.3 : set_local_transform
    EXPECT_EQ(ask(SHARD_IFACE_ECS, 2), SHARD_E_NOT_FOUND);
    EXPECT_EQ(ask(SHARD_IFACE_ACTORS, 1), SHARD_OK);                // 1.4 : actors, scripts, input (1.5 : one field of a component at a time)
    EXPECT_EQ(ask(SHARD_IFACE_ACTORS, 2), SHARD_E_NOT_FOUND);
    EXPECT_EQ(ask(SHARD_IFACE_SCRIPTS, 1), SHARD_OK);               // 1.5 : contacts, accessor strings
    EXPECT_EQ(ask(SHARD_IFACE_SCRIPTS, 2), SHARD_E_NOT_FOUND);
    EXPECT_EQ(ask(SHARD_IFACE_INPUT, 0), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_PHYSICS, 0), SHARD_OK);               // 1.5 : physics, audio, worlds
    EXPECT_EQ(ask(SHARD_IFACE_AUDIO, 0), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_WORLDS, 0), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_EDITOR, 0), SHARD_E_NOT_FOUND);       // only the editor has it
    EXPECT_EQ(SHARD_ABI_MINOR, 6);                                  // 1.6 : shard.editor 1.1 (menus, toolbar, inspectors, gizmos, selection, more widgets)
}

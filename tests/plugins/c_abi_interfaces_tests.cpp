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

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using nlohmann::json;
using PluginTest::TempDir;

namespace {
    const char* kId = "shard.test.cplugin";
    const char* kConsumer = "shard.test.cconsumer";

    std::string FixtureDir(const char* id) { return std::string(SHARD_TEST_PLUGIN_DIR) + "/" + id; }

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

TEST_F(CAbiTest, ComponentTypeIsActorAndEcs)
{
    Start();
    const std::string name = std::string(kId) + ".Counter";
    EXPECT_TRUE(Objects::Components::GetComponentRegistry().IsRegistered(name));
    const Core::Ecs::ComponentInfo& info = Core::Ecs::ComponentRegistry::Global().Info(CounterId());
    EXPECT_EQ(info.size, sizeof(Counter));
    EXPECT_EQ(info.name, "c:" + name);
    EXPECT_TRUE(Rec()->context->Systems().Has("tick"));

    uint32_t offset = 0, type = 0;
    auto describe = Export<int (*)(const char*, uint32_t*, uint32_t*)>("c_plugin_describe_field");
    EXPECT_EQ(describe("axis", &offset, &type), SHARD_OK);
    EXPECT_EQ(offset, offsetof(Counter, axis));
    EXPECT_EQ(type, SHARD_FIELD_VEC3);
    EXPECT_EQ(describe("nope", &offset, &type), SHARD_E_NOT_FOUND);

    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_FALSE(Objects::Components::GetComponentRegistry().IsRegistered(name));
    ASSERT_TRUE(pm().Enable(kId, &error)) << error;
    EXPECT_EQ(Int("c_plugin_setup_error"), 0);
    EXPECT_TRUE(Objects::Components::GetComponentRegistry().IsRegistered(name));
}

TEST_F(CAbiTest, CSystemRunsOverEntities)
{
    Start();
    BringUpEngine();
    Core::Ecs::Registry registry;
    Counter start{};
    start.rate = 4.0f;
    const Core::Ecs::Entity e = registry.Create();
    ASSERT_TRUE(registry.AddRaw(e, CounterId(), &start));
    registry.Create();

    RunFrame(registry, 0.5f);
    EXPECT_EQ(Int("c_plugin_system_runs"), 1);
    EXPECT_EQ(Export<uint64_t (*)()>("c_plugin_chunk_entities")(), 1u);
    const auto* after = static_cast<const Counter*>(registry.GetRaw(e, CounterId()));
    ASSERT_NE(after, nullptr);
    EXPECT_FLOAT_EQ(after->value, 2.0f);
    EXPECT_EQ(after->ticks, 1);

    EXPECT_EQ(registry.CountRaw({ CounterId() }, {}), 2u);
    RunFrame(registry, 0.5f);
    EXPECT_EQ(Export<uint64_t (*)()>("c_plugin_chunk_entities")(), 2u);

    EXPECT_EQ(Int("c_plugin_undeclared_get"), SHARD_E_INVALID_ARG);
    EXPECT_EQ(Int("c_plugin_wrong_thread"), SHARD_E_WRONG_THREAD);
    EXPECT_EQ(Int("c_plugin_frames_begun"), 2);
    EXPECT_EQ(Int("c_plugin_frames_ended"), 2);
    EXPECT_EQ(Rec()->errorCount, 0u);
}

TEST_F(CAbiTest, SystemLeavesWithPlugin)
{
    Start();
    BringUpEngine();
    const std::string full = std::string(kId) + "/tick";
    EXPECT_TRUE(scheduler.Has(full));
    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_FALSE(scheduler.Has(full));
}

TEST_F(CAbiTest, ComponentMirrorsEntityAndSaves)
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
        EXPECT_FLOAT_EQ(onEntity->rate, 2.0f);
        EXPECT_EQ(onEntity->enabled, 1);

        RunFrame(ecs, 1.0f);
        RunFrame(ecs, 1.0f);

        const ordered_json saved = component->Serialize();
        EXPECT_EQ(saved["type"], type);
        EXPECT_FLOAT_EQ(saved["value"].get<float>(), 4.0f);
        EXPECT_EQ(saved["ticks"].get<int>(), 2);
        EXPECT_FLOAT_EQ(saved["axis"]["y"].get<float>(), 1.0f);
        EXPECT_FALSE(saved.contains("scratch"));

        auto other = Core::Object::CreateWithContext<Objects::Actor>(&context, std::string("Copy"), &context);
        auto loaded = other->AddComponentByName(type);
        ASSERT_NE(loaded, nullptr);
        loaded->Deserialize(json::parse(saved.dump()));
        const auto* copy = static_cast<const Counter*>(ecs.GetRaw(other->GetEntity(), CounterId()));
        ASSERT_NE(copy, nullptr);
        EXPECT_FLOAT_EQ(copy->value, 4.0f);
        EXPECT_EQ(copy->ticks, 2);
        EXPECT_EQ(copy->scratch, 0u);

        loaded->DeActivate();
        EXPECT_EQ(ecs.GetRaw(other->GetEntity(), CounterId()), nullptr);
        loaded->Activate();
        ASSERT_NE(ecs.GetRaw(other->GetEntity(), CounterId()), nullptr);
        EXPECT_EQ(static_cast<const Counter*>(ecs.GetRaw(other->GetEntity(), CounterId()))->ticks, 2);

        auto clone = other->Clone();
        ASSERT_NE(clone, nullptr);
        const auto* cloned = static_cast<const Counter*>(ecs.GetRaw(clone->GetEntity(), CounterId()));
        ASSERT_NE(cloned, nullptr);
        EXPECT_EQ(cloned->ticks, 2);
    }
    Core::SetEngine(nullptr);
}

TEST_F(CAbiTest, SystemEventsArriveAtFrameEnd)
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
    EXPECT_EQ(heard, 2);
    EXPECT_EQ(ticks, 2);
    EXPECT_EQ(Int("c_plugin_ticks_received"), 2);
    EXPECT_EQ(Int("c_plugin_last_ticks"), 2);

    const int32_t value = 99;
    events.emitNamed("shard.test.cplugin.Ticked", &value, sizeof(value));
    EXPECT_EQ(Int("c_plugin_last_ticks"), 99);

    EXPECT_EQ(Int("c_plugin_unsubscribe"), SHARD_OK);
    EXPECT_EQ(Int("c_plugin_unsubscribe"), SHARD_E_NOT_FOUND);
    RunFrame(registry);
    EXPECT_EQ(Int("c_plugin_ticks_received"), 3);
    EXPECT_EQ(heard, 4);
}

TEST_F(CAbiTest, CHookCanCancel)
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
    EXPECT_TRUE(pm().BeforeSave("forbidden.world"));
}

TEST_F(CAbiTest, CServiceProvidedAndLost)
{
    config.pluginPaths.push_back(FixtureDir(kConsumer));
    Start();
    ASSERT_NE(Rec(kConsumer), nullptr);
    ASSERT_EQ(Rec(kConsumer)->state, PluginState::Initialized) << Rec(kConsumer)->error;
    EXPECT_EQ(Int("c_consumer_get_result", kConsumer), SHARD_OK);

    auto add = Export<int (*)(int, int, int*)>("c_consumer_add", kConsumer);
    int sum = 0;
    ASSERT_EQ(add(2, 3, &sum), SHARD_OK);
    EXPECT_EQ(sum, 1005);
    EXPECT_EQ(Export<unsigned (*)()>("c_consumer_version", kConsumer)(), 102u);

    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    ASSERT_EQ(Rec(kConsumer)->state, PluginState::Initialized);
    EXPECT_EQ(add(2, 3, &sum), SHARD_E_NOT_FOUND);
}

TEST_F(CAbiTest, CValidatorRuns)
{
    Start();
    BringUpEngine();
    const std::vector<std::string> problems = pm().RunToolValidators("project.json");
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0], "cplugin.check : checked project.json");
}

TEST_F(CAbiTest, CImporterAndAssetType)
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
    EXPECT_GT(Rec()->context->Memory().Used(), before);
    resource.reset();
    EXPECT_EQ(Int("c_plugin_unloaded_assets"), 1);
    EXPECT_EQ(Rec()->context->Memory().Used(), before);

    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_TRUE(pm().Extensions().Get<Filesystem::AssetImporterExtension>("asset.importer").empty());
}

TEST_F(CAbiTest, CToolTaskRefusal)
{
    Start();
    BringUpEngine();
    std::string message;
    EXPECT_FALSE(pm().NotifyToolTask("cook", "project.json", message));
    EXPECT_EQ(message, "cplugin refuses cooking");
    message.clear();
    EXPECT_TRUE(pm().NotifyToolTask("bake", "project.json", message));
}

TEST_F(CAbiTest, ReadsContentViaModUris)
{
    Start();
    EXPECT_EQ(Int("c_plugin_content_mounted"), 1);
    EXPECT_STREQ(Export<const char* (*)()>("c_plugin_content_text")(), "hello from content");
    EXPECT_EQ(Int("c_plugin_content_files"), 2);
    EXPECT_EQ(Rec()->context->Memory().Used(), 0u);
}

TEST_F(CAbiTest, HotReloadKeepsSavedState)
{
    config.allowHotReload = true;
    Start();
    BringUpEngine();
    std::string error;
    ASSERT_TRUE(Core::CVarRegistry::Global().Execute("cplugin.persist 42").ok);
    ASSERT_TRUE(pm().Reload(kId, &error)) << error;
    ASSERT_EQ(Rec()->state, PluginState::Enabled) << Rec()->error;
    EXPECT_EQ(Int("c_plugin_restored"), 42);
    EXPECT_EQ(Rec()->reloadCount, 1u);
}

TEST_F(CAbiTest, V1InterfacesOffered)
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

namespace {
    std::vector<uint32_t> gSeenActive;
    void RecordActive() { gSeenActive.push_back(PluginManager::Get().ActivePlugin()); }
}

TEST_F(CAbiTest, CallbacksNamedForCrashReport)
{
    Start();
    BringUpEngine();
    gSeenActive.clear();
    Export<void (*)(void (*)())>("c_plugin_set_probe")(&RecordActive);

    ASSERT_TRUE(Core::CVarRegistry::Global().Execute("cplugin.echo x").ok);
    const int32_t ticks = 1;
    events.emitNamed("shard.test.cplugin.Ticked", &ticks, sizeof(ticks));
    pm().BeforeSave("fine.world");
    pm().RunToolValidators("project.json");
    TempDir dir;
    std::filesystem::create_directories(dir.Sub("out"));
    pm().Extensions().Get<Filesystem::AssetImporterExtension>("asset.importer")[0]->import("a.cpin", dir.Sub("out"));
    Filesystem::AssetInfos infos;
    pm().Extensions().Get<Core::Resources::AssetTypeExtension>("asset.type")[0]->info.load("a.cpasset", infos).reset();
    Export<void (*)(void (*)())>("c_plugin_set_probe")(nullptr);

    ASSERT_EQ(gSeenActive.size(), 7u);
    for (uint32_t active : gSeenActive) EXPECT_EQ(active, Rec()->owner);
    EXPECT_EQ(pm().ActivePlugin(), 0u);
}

TEST_F(CAbiTest, HostIdentifiesEngine)
{
    const ShardHostApi* host = GetCHostApi();
    EXPECT_EQ(std::string(host->engineVersion.data, size_t(host->engineVersion.size)), VERSION);
}

namespace {
    struct Chunk {
        uint32_t count;
        uint32_t pad;
        uint64_t voxels;
    };
}

TEST_F(CAbiTest, BlobSavedCopiedFreed)
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
        EXPECT_EQ(block->voxels, 0u);

        const uint64_t blob = makeBlob(1000);
        ASSERT_NE(blob, 0u);
        block->voxels = blob;
        block->count = 7;
        EXPECT_GE(Rec()->context->Memory().Used(), memoryBefore + 1000);

        const ordered_json saved = component->Serialize();
        EXPECT_EQ(saved["count"].get<uint32_t>(), 7u);
        ASSERT_TRUE(saved["voxels"].is_string());
        EXPECT_EQ(saved["voxels"].get<std::string>().size(), 1336u);     // base64 of 1000 bytes

        auto other = Core::Object::CreateWithContext<Objects::Actor>(&context, std::string("Loaded"), &context);
        auto loaded = other->AddComponentByName(type);
        loaded->Deserialize(json::parse(saved.dump()));
        const auto* loadedBlock = static_cast<const Chunk*>(ecs.GetRaw(other->GetEntity(), chunkId));
        ASSERT_NE(loadedBlock, nullptr);
        EXPECT_NE(loadedBlock->voxels, blob);
        EXPECT_EQ(checkBlob(loadedBlock->voxels, 1000), SHARD_OK);
        EXPECT_EQ(loadedBlock->count, 7u);

        auto clone = other->Clone();
        const auto* cloneBlock = static_cast<const Chunk*>(ecs.GetRaw(clone->GetEntity(), chunkId));
        ASSERT_NE(cloneBlock, nullptr);
        EXPECT_NE(cloneBlock->voxels, loadedBlock->voxels);
        EXPECT_EQ(checkBlob(cloneBlock->voxels, 1000), SHARD_OK);

        actor->RemoveComponent(component);
        EXPECT_EQ(checkBlob(blob, 1000), SHARD_E_STALE_HANDLE);

        const uint64_t loose = makeBlob(10);
        EXPECT_EQ(Export<int (*)(uint64_t, uint32_t)>("c_plugin_resize_and_free")(loose, 4), SHARD_OK);
        EXPECT_EQ(checkBlob(loose, 4), SHARD_E_STALE_HANDLE);

        const uint64_t kept = loadedBlock->voxels;
        std::string error;
        ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
        ASSERT_TRUE(pm().Enable(kId, &error)) << error;
        EXPECT_EQ(checkBlob(kept, 1000), SHARD_E_STALE_HANDLE);
    }
    Core::SetEngine(nullptr);
}

TEST_F(CAbiTest, MainThreadRegistryExperimental)
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
    EXPECT_EQ(ecs.CountRaw({ CounterId() }, {}), 2u);
    EXPECT_EQ(Core::CVarRegistry::Global().Find("shard.test.cplugin.counter")->ToString(), "3");

    const ShardHostApi* host = GetCHostApi();
    const void* table = nullptr;
    const ShardStr id{ SHARD_IFACE_EXPERIMENTAL_WORLD_ECS, std::strlen(SHARD_IFACE_EXPERIMENTAL_WORLD_ECS) };
    EXPECT_EQ(host->query_interface(id, 0, 1, &table), SHARD_OK);
    EXPECT_EQ(host->query_interface(id, 0, 0, &table), SHARD_E_NOT_FOUND);
    EXPECT_EQ(host->query_interface(id, 1, 1, &table), SHARD_E_NOT_FOUND);
}

TEST_F(CAbiTest, DistributedRefusedExperimental)
{
    using Status = SignatureInfo::Status;
    EXPECT_FALSE(MayUseExperimental(TrustTier::Native, Status::Valid));
    EXPECT_TRUE(MayUseExperimental(TrustTier::Native, Status::Unsigned));
    EXPECT_TRUE(MayUseExperimental(TrustTier::Native, Status::Untrusted));
    EXPECT_TRUE(MayUseExperimental(TrustTier::FirstParty, Status::Valid));
}

TEST_F(CAbiTest, EventShownWithFields)
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
    EXPECT_EQ(events.namedLayout("shard.test.cplugin.Broken"), nullptr);

    auto& console = Core::CVarRegistry::Global();
    const Core::CommandResult list = console.Execute("event.list");
    EXPECT_TRUE(list.ok);
    EXPECT_NE(list.output.find("shard.test.cplugin.Ticked (4 bytes) : ticks@0"), std::string::npos) << list.output;
    EXPECT_TRUE(console.Execute("event.watch shard.test.cplugin.Ticked").ok);
    events.emitNamed("shard.test.cplugin.Ticked", &ticks, sizeof(ticks));
    EXPECT_EQ(Int("c_plugin_last_ticks"), 5);
    EXPECT_TRUE(console.Execute("event.unwatch shard.test.cplugin.Ticked").ok);
    EXPECT_FALSE(console.Execute("event.unwatch shard.test.cplugin.Ticked").ok);

    std::string error;
    ASSERT_TRUE(pm().Disable(kId, true, &error)) << error;
    EXPECT_EQ(events.namedLayout("shard.test.cplugin.Ticked"), nullptr);
}

namespace {
    struct Label {
        uint64_t name;
        int32_t icon;
        int32_t visits;
    };
}

TEST_F(CAbiTest, TextAndAssetOwnedByComponent)
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

        component->Deserialize(json::parse(R"({"active":true,"name":"hello","icon":7,"visits":3})"));
        auto* block = static_cast<Label*>(ecs.GetRaw(actor->GetEntity(), labelId));
        ASSERT_NE(block, nullptr);
        EXPECT_EQ(block->icon, 7);
        EXPECT_EQ(block->visits, 3);
        ASSERT_NE(block->name, 0u);
        EXPECT_EQ(textOf(block->name), "hello");

        const uint64_t first = block->name;
        component->Deserialize(json::parse(R"({"active":true,"name":"edited a lot","icon":7,"visits":3})"));
        block = static_cast<Label*>(ecs.GetRaw(actor->GetEntity(), labelId));
        EXPECT_EQ(block->name, first);
        EXPECT_EQ(textOf(block->name), "edited a lot");

        RunFrame(ecs);
        RunFrame(ecs);
        EXPECT_EQ(Int("c_plugin_label_result"), 0);
        const ordered_json saved = component->Serialize();
        EXPECT_EQ(saved["name"].get<std::string>(), "edited a lot!!");
        EXPECT_EQ(saved["visits"].get<int>(), 5);
        EXPECT_TRUE(saved.contains("icon"));

        auto clone = actor->Clone();
        const auto* cloned = static_cast<const Label*>(ecs.GetRaw(clone->GetEntity(), labelId));
        ASSERT_NE(cloned, nullptr);
        EXPECT_NE(cloned->name, block->name);
        EXPECT_EQ(textOf(cloned->name), "edited a lot!!");
        EXPECT_EQ(cloned->icon, 7);

        const uint64_t owned = block->name;
        actor->RemoveComponent(component);
        EXPECT_EQ(textOf(owned), "<stale>");
    }
    Core::SetEngine(nullptr);
}

TEST_F(CAbiTest, SystemMovesActorAtPhaseEnd)
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

        const Core::Ecs::Entity bare = ecs.Create();
        const Label none{};
        ASSERT_TRUE(ecs.AddRaw(bare, labelId, &none));

        RunFrame(ecs);
        EXPECT_FLOAT_EQ(actor->transform->GetPosition().x, 1.0f);
        EXPECT_FLOAT_EQ(actor->transform->GetScale().y, 2.0f);
        const auto* mirror = ecs.TryGet<Worlds::LocalTransform>(actor->GetEntity());
        ASSERT_NE(mirror, nullptr);
        EXPECT_FLOAT_EQ(mirror->position.x, 1.0f);
        EXPECT_FLOAT_EQ(mirror->scale.z, 2.0f);
        RunFrame(ecs);
        EXPECT_FLOAT_EQ(actor->transform->GetPosition().x, 2.0f);

        EXPECT_EQ(Int("c_plugin_label_result"), 0);
        EXPECT_EQ(Int("c_plugin_label_no_actor"), 2);
        EXPECT_EQ(Int("c_plugin_undeclared_pose"), SHARD_E_INVALID_ARG);
        EXPECT_EQ(Rec()->errorCount, 0u);

        actor->transform->DeActivate();
        RunFrame(ecs);
        EXPECT_FLOAT_EQ(actor->transform->GetPosition().x, 2.0f);
    }
    Core::SetEngine(nullptr);
}

TEST_F(CAbiTest, RevisionInterfacesOffered)
{
    Start();
    const ShardHostApi* api = GetCHostApi();
    const void* table = nullptr;
    auto ask = [&](const char* id, uint32_t minor) { return api->query_interface(ShardStr{ id, std::strlen(id) }, 1, minor, &table); };
    EXPECT_EQ(ask(SHARD_IFACE_COMPONENTS, 4), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_COMPONENTS, 5), SHARD_E_NOT_FOUND);
    EXPECT_EQ(ask(SHARD_IFACE_ECS, 1), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_ECS, 2), SHARD_E_NOT_FOUND);
    EXPECT_EQ(ask(SHARD_IFACE_ACTORS, 2), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_ACTORS, 3), SHARD_E_NOT_FOUND);
    EXPECT_EQ(ask(SHARD_IFACE_SCRIPTS, 2), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_SCRIPTS, 3), SHARD_E_NOT_FOUND);
    EXPECT_EQ(ask(SHARD_IFACE_INPUT, 1), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_INPUT, 2), SHARD_E_NOT_FOUND);
    EXPECT_EQ(ask(SHARD_IFACE_PHYSICS, 1), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_PHYSICS, 2), SHARD_E_NOT_FOUND);
    EXPECT_EQ(ask(SHARD_IFACE_AUDIO, 0), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_WORLDS, 1), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_COMMANDS, 1), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_JOBS, 0), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_TIME, 0), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_TIMERS, 0), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_TIMERS, 1), SHARD_E_NOT_FOUND);
    EXPECT_EQ(ask(SHARD_IFACE_ASSETS, 0), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_RENDER, 0), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_DEBUG, 0), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_REFLECT, 0), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_NET, 0), SHARD_OK);
    EXPECT_EQ(ask(SHARD_IFACE_NET, 1), SHARD_E_NOT_FOUND);
    EXPECT_EQ(ask(SHARD_IFACE_EDITOR, 0), SHARD_E_NOT_FOUND);
    EXPECT_EQ(SHARD_ABI_MINOR, 8);
}

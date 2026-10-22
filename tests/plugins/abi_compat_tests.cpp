#include <gtest/gtest.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <shard/shard.h>

#include "engine/core/ecs/registry.hpp"
#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/plugins/manager.hpp"
#include "plugins/plugin_test_utils.hpp"

// Compatibility of the C ABI across its revisions, and what the host does with garbage :
//   - a plugin built against the frozen 1.0 headers (tests/plugin_fixtures/abi_1_0) loads and works in this engine (1.x, x >= 0) ;
//   - a plugin built against these headers (1.1) is refused by an engine of 1.0 ;
//   - the loader and every function of every table survive inconsistent structs, null pointers and handles that mean nothing.

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;

namespace {
    std::string FixtureDir(const char* id) { return std::string(SHARD_TEST_PLUGIN_DIR) + "/" + id; }

    class AbiCompatTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            PluginManager::Get().ResetForTests();
            config.host = Host::Headless;
            config.registerConsoleCommands = false;
        }
        void TearDown() override { PluginManager::Get().ResetForTests(); }

        PluginManager& pm() { return PluginManager::Get(); }
        template <typename Fn>
        Fn Export(const char* id, const char* name) { return pm().Find(id)->library.Symbol<Fn>(name); }

        PluginManagerConfig config;
        // Members : the plugins' systems are removed from them in TearDown
        Core::Ecs::Scheduler scheduler;
        Events::EventDispatcher events;
    };

    ShardStr S(const char* text) { return ShardStr{ text, std::strlen(text) }; }
}

TEST_F(AbiCompatTest, APluginOfTheAbi10LoadsAndWorksInThisEngine)
{
    const char* id = "shard.test.abi10";
    config.pluginPaths.push_back(FixtureDir(id));
    std::string error;
    ASSERT_TRUE(pm().Startup(config, &error)) << error;
    PluginRecord* r = pm().Find(id);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(r->state, PluginState::Initialized) << r->error;
    EXPECT_EQ(Export<unsigned (*)()>(id, "abi10_built_minor")(), 0u);
    EXPECT_EQ(Export<int (*)()>(id, "abi10_setup")(), 0);
    EXPECT_STREQ(Export<const char* (*)()>(id, "abi10_id")(), id);

    // The tables grew since : the plugin was handed the bigger one, and reads only the part it knows
    EXPECT_LT(Export<unsigned (*)()>(id, "abi10_own_plugin_api_size")(), Export<unsigned (*)()>(id, "abi10_host_plugin_api_size")());
    EXPECT_EQ(Export<unsigned (*)()>(id, "abi10_host_plugin_api_size")(), sizeof(ShardPluginApi));
    EXPECT_EQ(Export<unsigned (*)()>(id, "abi10_own_callbacks_size")(), sizeof(ShardPluginCallbacks));     // unchanged in 1.1

    // Its system runs over its component
    pm().OnEngineSystemsReady(nullptr, &scheduler, &events);
    pm().OnEngineInitialized();
    ASSERT_EQ(r->state, PluginState::Enabled) << r->error;
    Core::Ecs::Registry registry;
    const auto type = Core::Ecs::ComponentId(Export<uint64_t (*)()>(id, "abi10_type")() - 1);
    const float start[2] = { 1.0f, 0.0f };
    const Core::Ecs::Entity e = registry.Create();
    ASSERT_TRUE(registry.AddRaw(e, type, start));
    Core::Ecs::Scheduler::FrameTiming timing;
    scheduler.RunFrame(registry, nullptr, timing);
    EXPECT_EQ(Export<int (*)()>(id, "abi10_runs")(), 1);
    EXPECT_FLOAT_EQ(static_cast<const float*>(registry.GetRaw(e, type))[0], 2.0f);
}

TEST_F(AbiCompatTest, APluginOfTheCurrentAbiIsRefusedByAnEngineOf10)
{
    // What the 1.1 plugin's entry point fills, checked the way a 1.0 engine would
    config.pluginPaths.push_back(FixtureDir("shard.test.cplugin"));
    ASSERT_TRUE(pm().Startup(config));
    PluginRecord* r = pm().Find("shard.test.cplugin");
    ASSERT_NE(r, nullptr);
    auto entry = r->library.Symbol<ShardPluginEntryFn>("shard_plugin_entry");
    ASSERT_NE(entry, nullptr);
    ShardPluginInfo info;
    std::memset(&info, 0, sizeof(info));
    ASSERT_EQ(entry(GetCHostApi(), &info), SHARD_OK);
    EXPECT_EQ(info.abiMinor, uint32_t(SHARD_ABI_MINOR));
    EXPECT_EQ(ValidateCPluginInfo(info), "");                       // this engine : fine
    EXPECT_EQ(ValidateCPluginInfo(info, 0), "built for the C ABI 1." + std::to_string(SHARD_ABI_MINOR) + ", this engine has 1.0");
}

TEST_F(AbiCompatTest, AnInterfaceIsOfferedUpToItsRevision)
{
    const ShardHostApi* host = GetCHostApi();
    EXPECT_EQ(host->abiMajor, 1u);
    EXPECT_EQ(host->abiMinor, uint32_t(SHARD_ABI_MINOR));
    const void* table = nullptr;
    EXPECT_EQ(host->query_interface(S(SHARD_IFACE_PLUGIN), 1, 0, &table), SHARD_OK);     // a 1.0 plugin
    EXPECT_EQ(host->query_interface(S(SHARD_IFACE_PLUGIN), 1, 1, &table), SHARD_OK);     // a 1.1 plugin
    EXPECT_EQ(static_cast<const ShardPluginApi*>(table)->structSize, sizeof(ShardPluginApi));
    EXPECT_EQ(host->query_interface(S(SHARD_IFACE_PLUGIN), 1, 2, &table), SHARD_OK);     // 1.5 : random streams
    EXPECT_EQ(host->query_interface(S(SHARD_IFACE_PLUGIN), 1, 3, &table), SHARD_E_NOT_FOUND);
    EXPECT_EQ(table, nullptr);
    EXPECT_EQ(host->query_interface(S(SHARD_IFACE_SYSTEMS), 1, 1, &table), SHARD_E_NOT_FOUND);  // still 1.0
    EXPECT_EQ(host->query_interface(S("shard.nothing"), 1, 0, &table), SHARD_E_NOT_FOUND);
    EXPECT_EQ(host->query_interface(S(SHARD_IFACE_PLUGIN), 1, 0, nullptr), SHARD_E_INVALID_ARG);
}

// ---- Fuzz ----

TEST_F(AbiCompatTest, TheLoaderRefusesInconsistentInfoWithoutCrashing)
{
    std::mt19937 rng(1234);
    ShardPluginCallbacks callbacks{};
    callbacks.structSize = sizeof(callbacks);
    ShardPluginCallbacks tiny{};
    tiny.structSize = 2;
    const ShardPluginCallbacks* tables[] = { nullptr, &callbacks, &tiny };
    int accepted = 0;
    for (int i = 0; i < 20000; ++i) {
        ShardPluginInfo info;
        std::memset(&info, 0, sizeof(info));
        const uint32_t sizes[] = { 0u, 4u, 8u, 40u, 47u, 48u, 49u, 4096u, uint32_t(rng()) };
        info.structSize = sizes[rng() % 9];
        info.abiMajor = rng() % 4 == 0 ? uint32_t(rng()) : uint32_t(rng() % 3);
        info.abiMinor = rng() % 4 == 0 ? uint32_t(rng()) : uint32_t(rng() % 3);
        info.callbacks = tables[rng() % 3];
        const std::string problem = ValidateCPluginInfo(info);
        const bool fine = info.structSize >= sizeof(ShardPluginInfo) && info.abiMajor == 1 && info.abiMinor <= SHARD_ABI_MINOR && info.callbacks &&
                          info.callbacks->structSize >= sizeof(uint32_t);
        EXPECT_EQ(problem.empty(), fine) << "size " << info.structSize << " abi " << info.abiMajor << "." << info.abiMinor << " : " << problem;
        if (fine) {
            ++accepted;
            // The adapter copies no more of the callbacks than their structSize says
            delete CreateCPlugin(1, info);
        }
    }
    EXPECT_GT(accepted, 0);
}

TEST_F(AbiCompatTest, EveryFunctionSurvivesHandlesThatMeanNothing)
{
    // No plugin is bound : every ShardPluginCtx, ShardEcs, ShardState... is stale. Nothing may crash ; every result is an error.
    const ShardHostApi* host = GetCHostApi();
    auto get = [&](const char* id) { const void* t = nullptr; host->query_interface(S(id), 1, 0, &t); return t; };
    const auto* plugin = static_cast<const ShardPluginApi*>(get(SHARD_IFACE_PLUGIN));
    const auto* cvars = static_cast<const ShardCVarsApi*>(get(SHARD_IFACE_CVARS));
    const auto* commands = static_cast<const ShardCommandsApi*>(get(SHARD_IFACE_COMMANDS));
    const auto* components = static_cast<const ShardComponentsApi*>(get(SHARD_IFACE_COMPONENTS));
    const auto* ecs = static_cast<const ShardEcsApi*>(get(SHARD_IFACE_ECS));
    const auto* systems = static_cast<const ShardSystemsApi*>(get(SHARD_IFACE_SYSTEMS));
    const auto* events = static_cast<const ShardEventsApi*>(get(SHARD_IFACE_EVENTS));
    const auto* hooks = static_cast<const ShardHooksApi*>(get(SHARD_IFACE_HOOKS));
    const auto* services = static_cast<const ShardServicesApi*>(get(SHARD_IFACE_SERVICES));
    const auto* extensions = static_cast<const ShardExtensionsApi*>(get(SHARD_IFACE_EXTENSIONS));
    const auto* content = static_cast<const ShardContentApi*>(get(SHARD_IFACE_CONTENT));
    const auto* state = static_cast<const ShardStateApi*>(get(SHARD_IFACE_STATE));
    const auto* actors = static_cast<const ShardActorsApi*>(get(SHARD_IFACE_ACTORS));
    const auto* scripts = static_cast<const ShardScriptsApi*>(get(SHARD_IFACE_SCRIPTS));
    const auto* input = static_cast<const ShardInputApi*>(get(SHARD_IFACE_INPUT));
    const auto* physics = static_cast<const ShardPhysicsApi*>(get(SHARD_IFACE_PHYSICS));
    const auto* audio = static_cast<const ShardAudioApi*>(get(SHARD_IFACE_AUDIO));
    const auto* worlds = static_cast<const ShardWorldsApi*>(get(SHARD_IFACE_WORLDS));
    ASSERT_TRUE(plugin && cvars && commands && components && ecs && systems && events && hooks && services && extensions && content && state && actors && scripts && input &&
                physics && audio && worlds);

    std::mt19937_64 rng(99);
    const ShardStr name = S("x");
    const ShardStr empty{ nullptr, 0 };
    for (int i = 0; i < 2000; ++i) {
        const uint64_t h = i % 3 == 0 ? 0 : rng();
        char buffer[8];
        uint64_t size = 0;
        void* p = nullptr;
        // base
        host->log(h, uint32_t(rng()), name);
        EXPECT_EQ(host->alloc(h, 64, 16), nullptr);
        host->free(h, &size);
        EXPECT_EQ(plugin->id(h).data, nullptr);
        EXPECT_EQ(plugin->host(h).data, nullptr);
        EXPECT_EQ(plugin->has_permission(h, name), SHARD_FALSE);
        ShardRandomState randomState{};
        EXPECT_LT(plugin->random_stream(h, name, &randomState), 0);
        EXPECT_LT(plugin->random_stream(h, name, nullptr), 0);
        // cvars and commands
        ShardCVar cv = 0;
        EXPECT_LT(cvars->create_int(h, name, 0, empty, 0, 0, 1, &cv), 0);
        EXPECT_LT(cvars->get_string(h, rng(), buffer, sizeof(buffer), &size), 0);
        EXPECT_LT(cvars->set_float(h, rng(), 1.f), 0);
        EXPECT_LT(commands->add(h, name, empty, nullptr, nullptr), 0);
        // components, ecs, systems
        ShardComponentType type = 0;
        EXPECT_LT(components->register_type(h, nullptr, &type), 0);
        EXPECT_LT(components->find_type(h, name, &type), 0);
        EXPECT_LT(components->field_count(h, rng(), nullptr), 0);
        ShardFieldDesc field{};
        field.structSize = sizeof(field);
        EXPECT_LT(components->field_at(h, rng(), 0, &field), 0);
        ShardQuery query{};
        query.structSize = sizeof(query);
        EXPECT_LT(ecs->each_chunk(h, &query, [](void*, const ShardChunk*) -> ShardResult { return SHARD_OK; }, nullptr), 0);
        EXPECT_EQ(ecs->is_alive(h, rng()), SHARD_FALSE);
        EXPECT_LT(ecs->get(h, rng(), rng(), &p), 0);
        ShardEntity spawned = 0;
        EXPECT_LT(ecs->spawn(h, &spawned), 0);
        EXPECT_LT(ecs->add(h, rng(), rng(), buffer, sizeof(buffer)), 0);
        ShardLocalTransform pose{};
        EXPECT_LT(ecs->set_local_transform(h, rng(), &pose), 0);
        EXPECT_LT(ecs->set_local_transform(h, rng(), nullptr), 0);
        ShardBlob text = rng();
        ShardStr got{ nullptr, 0 };
        EXPECT_LT(components->string_set(h, &text, name), 0);
        EXPECT_LT(components->string_set(h, nullptr, name), 0);
        EXPECT_LT(components->string_get(h, rng(), &got), 0);
        EXPECT_LT(components->string_get(h, 0, nullptr), 0);
        EXPECT_LT(systems->add(h, nullptr), 0);
        EXPECT_LT(systems->set_enabled(h, name, 1), 0);
        // actors, scripts, input : handles of actors that never existed
        ShardActor found = 0;
        uint32_t counted = 0;
        EXPECT_EQ(actors->is_valid(rng()), SHARD_FALSE);
        EXPECT_LT(actors->get_name(rng(), buffer, sizeof(buffer), &size), 0);
        EXPECT_LT(actors->set_name(rng(), name), 0);
        EXPECT_LT(actors->set_active(rng(), 1), 0);
        EXPECT_LT(actors->destroy(rng()), 0);
        EXPECT_LT(actors->get_transform(rng(), &pose), 0);
        EXPECT_LT(actors->set_transform(rng(), &pose), 0);
        EXPECT_LT(actors->set_world_transform(rng(), nullptr), 0);
        EXPECT_LT(actors->get_parent(rng(), &found), 0);
        EXPECT_LT(actors->set_parent(rng(), rng(), 1), 0);
        EXPECT_LT(actors->child_count(rng(), &counted), 0);
        EXPECT_LT(actors->add_tag(rng(), name), 0);
        EXPECT_EQ(actors->has_tag(rng(), name), SHARD_FALSE);
        EXPECT_LT(actors->create(rng(), name, 0, &found), 0);
        EXPECT_LT(actors->spawn(rng(), name, 0, &found), 0);
        EXPECT_LT(actors->find_by_name(rng(), name, &found), 0);
        EXPECT_LT(actors->find_by_tag(rng(), name, nullptr, 0, &counted), 0);
        EXPECT_LT(actors->add_component(rng(), name, empty), 0);
        EXPECT_EQ(actors->has_component(rng(), name), SHARD_FALSE);
        EXPECT_LT(actors->get_component(rng(), name, buffer, sizeof(buffer), &size), 0);
        EXPECT_LT(actors->set_component(rng(), name, empty), 0);
        EXPECT_LT(actors->get_component_field(rng(), name, name, buffer, sizeof(buffer), &size), 0);
        EXPECT_LT(actors->set_component_field(rng(), name, name, empty), 0);
        EXPECT_LT(actors->erase_component_field(rng(), name, name), 0);
        EXPECT_LT(scripts->register_script(h, nullptr), 0);
        EXPECT_LT(scripts->get_object(h, rng(), name, &p), 0);
        EXPECT_LT(scripts->add_to_actor(h, rng(), name, &p), 0);
        // physics, audio, worlds : the same
        const float vec[3] = { 1.f, 2.f, 3.f };
        float out3[3] = {};
        ShardRayHit rayHit{};
        rayHit.structSize = sizeof(rayHit);
        EXPECT_LT(physics->get_velocity(rng(), out3), 0);
        EXPECT_LT(physics->set_velocity(rng(), vec), 0);
        EXPECT_LT(physics->add_force(rng(), vec), 0);
        EXPECT_LT(physics->add_impulse_at_point(rng(), vec, vec), 0);
        EXPECT_LT(physics->get_gravity(rng(), out3), 0);
        EXPECT_LT(physics->set_custom_gravity(rng(), nullptr), 0);
        EXPECT_LT(physics->raycast(nullptr, vec, 1.f, SHARD_LAYER_ALL, 0, &rayHit), 0);
        EXPECT_LT(physics->raycast(vec, vec, 1.f, SHARD_LAYER_ALL, rng(), &rayHit), 0);
        EXPECT_LT(audio->play(rng()), 0);
        EXPECT_LT(audio->pause(rng()), 0);
        EXPECT_LT(audio->set_volume(rng(), 1.f), 0);
        EXPECT_LT(audio->play_one_shot(name, nullptr, 1.f, 1), 0);
        EXPECT_LT(worlds->load_world(empty), 0);
        EXPECT_LT(worlds->spawn_prefab(rng(), name, 0, &found), 0);
        EXPECT_EQ(input->key_down(int32_t(rng())), SHARD_FALSE);
        EXPECT_EQ(input->mouse_pressed(int32_t(rng())), SHARD_FALSE);
        EXPECT_LT(input->get_cursor(nullptr, nullptr), 0);
        EXPECT_LT(input->set_cursor_mode(int32_t(rng())), 0);
        EXPECT_EQ(input->gamepad_button_down(int32_t(rng()), int32_t(rng())), SHARD_FALSE);
        // events, hooks, services
        ShardSubscription sub = 0;
        EXPECT_LT(events->subscribe(h, name, nullptr, nullptr, &sub), 0);
        EXPECT_LT(events->emit(h, name, nullptr, 0), 0);
        EXPECT_LT(events->unsubscribe(h, rng()), 0);
        ShardHookToken token = 0;
        EXPECT_LT(hooks->on(h, name, 0, nullptr, nullptr, &token), 0);
        EXPECT_LT(hooks->off(h, rng()), 0);
        ShardServiceRef ref = 0;
        EXPECT_LT(services->provide(h, name, 1, 0, nullptr, nullptr), 0);
        EXPECT_LT(services->get(h, name, empty, &ref), 0);
        ShardService service{};
        service.structSize = sizeof(service);
        EXPECT_LT(services->resolve(h, rng(), &service), 0);
        EXPECT_LT(services->release(h, rng()), 0);
        // extensions, content, state
        EXPECT_LT(extensions->add_validator(h, nullptr), 0);
        EXPECT_LT(extensions->remove(h, name, name), 0);
        EXPECT_LT(extensions->get_asset(h, name, name, &p), 0);
        EXPECT_LT(content->resolve(h, name, buffer, sizeof(buffer), &size), 0);
        EXPECT_LT(content->read(h, name, &p, &size), 0);
        EXPECT_LT(state->set(rng(), name, S("1")), 0);
        EXPECT_LT(state->get(rng(), name, buffer, sizeof(buffer), &size), 0);
        EXPECT_EQ(state->has(rng(), name), SHARD_FALSE);
    }
}

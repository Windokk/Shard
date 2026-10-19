#include <gtest/gtest.h>

#include "plugins/plugin_test_utils.hpp"
#include "support/test_engine_context.hpp"

#include "engine/plugins/declared_component.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/world/actor.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using nlohmann::json;
using PluginTest::TempDir;

namespace {
    DeclaredComponent Declare(const std::string& jsonText)
    {
        auto parsed = ParseDeclaredComponent(json::parse(jsonText));
        EXPECT_TRUE(parsed) << (parsed ? "" : parsed.GetError().message);
        return parsed ? parsed.Value() : DeclaredComponent();
    }

    const char* kHealth = R"({ "name": "Health", "fields": {
        "hp": { "type": "float", "default": 80 }, "maxHp": { "type": "int", "default": 100 }, "alive": { "type": "bool", "default": true },
        "label": { "type": "string", "default": "player" }, "offset": { "type": "vec3", "default": [1, 2, 3] }, "tint": { "type": "color", "default": [0.5, 0.25, 1] } } })";

    class DeclaredComponentTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            Core::SetEngine(&context);
            actor = Core::Object::CreateWithContext<Objects::Actor>(&context, std::string("Subject"), &context);
        }
        void TearDown() override
        {
            actor.reset();
            Core::SetEngine(nullptr);
        }
        std::shared_ptr<DeclaredFieldsComponent> Make(const std::string& text = kHealth)
        {
            auto type = DeclaredComponentType::Build(Declare(text));
            EXPECT_TRUE(type) << (type ? "" : type.GetError().message);
            auto component = std::dynamic_pointer_cast<DeclaredFieldsComponent>(MakeDeclaredFactory(type.Value())());
            EXPECT_NE(component, nullptr);
            return component;
        }
        Shard::Tests::TestEngineContext context;
        std::shared_ptr<Objects::Actor> actor;
    };
}

TEST_F(DeclaredComponentTest, StartsAtItsDeclaredDefaults)
{
    auto c = Make();
    EXPECT_EQ(c->Get("hp"), 80.0);
    EXPECT_EQ(c->Get("maxHp"), 100);
    EXPECT_EQ(c->Get("alive"), true);
    EXPECT_EQ(c->Get("label"), "player");
    EXPECT_EQ(c->Get("offset"), json::array({ 1.0, 2.0, 3.0 }));
    EXPECT_EQ(c->Get("tint"), json::array({ 0.5, 0.25, 1.0, 1.0 }));          // a color given without alpha is opaque
    EXPECT_TRUE(c->Get("nothing").is_null());
    EXPECT_EQ(c->GetDescriptor()->name, "Health");
    EXPECT_EQ(c->GetDescriptor()->fields.size(), 6u);
}

TEST_F(DeclaredComponentTest, SetChecksTheTypeOfTheValue)
{
    auto c = Make();
    EXPECT_TRUE(c->Set("hp", 12.5));
    EXPECT_TRUE(c->Set("label", "a rather long string that does not fit in the small string buffer of std::string"));
    EXPECT_TRUE(c->Set("offset", json::array({ 9, 8, 7 })));
    EXPECT_TRUE(c->Set("alive", false));
    EXPECT_FALSE(c->Set("hp", "text"));
    EXPECT_FALSE(c->Set("maxHp", 1.5));
    EXPECT_FALSE(c->Set("offset", json::array({ 1, 2 })));
    EXPECT_FALSE(c->Set("ghost", 1));
    EXPECT_DOUBLE_EQ(c->Get("hp").get<double>(), 12.5);
    EXPECT_EQ(c->Get("label").get<std::string>().size(), 80u);
    EXPECT_EQ(c->Get("alive"), false);
    EXPECT_EQ(c->Get("maxHp"), 100);
}

TEST_F(DeclaredComponentTest, SavesAndLoadsThroughReflection)
{
    auto c = Make();
    actor->AddComponentRaw(c);
    c->Set("hp", 33.0);
    c->Set("label", "saved");
    c->Set("tint", json::array({ 0.1, 0.2, 0.3, 0.4 }));

    const ordered_json saved = c->Serialize();
    EXPECT_EQ(saved["type"], "Health");
    EXPECT_EQ(saved["active"], true);
    EXPECT_DOUBLE_EQ(saved["hp"].get<double>(), 33.0);
    EXPECT_EQ(saved["label"], "saved");
    EXPECT_EQ(saved["maxHp"], 100);

    auto loaded = Make();
    actor->AddComponentRaw(loaded);
    loaded->Deserialize(json::parse(saved.dump()));
    EXPECT_EQ(loaded->Values(), c->Values());

    // a field missing from the file keeps its default, an unknown one is ignored
    auto partial = Make();
    actor->AddComponentRaw(partial);
    partial->Deserialize(json::parse(R"({ "type": "Health", "hp": 5, "unknownField": 1 })"));
    EXPECT_DOUBLE_EQ(partial->Get("hp").get<double>(), 5.0);
    EXPECT_EQ(partial->Get("label"), "player");
}

TEST_F(DeclaredComponentTest, CloningCopiesTheFieldsNotTheStorage)
{
    auto c = Make();
    c->Set("label", "original label that is long enough to live on the heap, not inline");
    c->Set("hp", 1.0);
    auto copy = std::dynamic_pointer_cast<DeclaredFieldsComponent>(c->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Values(), c->Values());
    copy->Set("label", "changed");
    EXPECT_NE(copy->Get("label"), c->Get("label"));
    c.reset();                       // the copy owns its strings
    EXPECT_EQ(copy->Get("label"), "changed");
}

TEST_F(DeclaredComponentTest, TheEditorSeesEditableFieldsOfTheRightTypes)
{
    auto c = Make();
    std::map<std::string, TypeID> types;
    for (const FieldInfo* f : c->GetDescriptor()->fields) {
        types[f->name] = f->type;
        EXPECT_TRUE(f->flags & Editable) << f->name;
        EXPECT_NE(f->CopyConstruct, nullptr);
        EXPECT_NE(f->Assign, nullptr);
        EXPECT_NE(f->Destroy, nullptr);
    }
    EXPECT_EQ(types["hp"], TypeID::Float);
    EXPECT_EQ(types["maxHp"], TypeID::Int32);
    EXPECT_EQ(types["alive"], TypeID::Bool);
    EXPECT_EQ(types["label"], TypeID::String);
    EXPECT_EQ(types["offset"], TypeID::Vec3);
    EXPECT_EQ(types["tint"], TypeID::ColorRGBA);
}

TEST(DeclaredComponentBuild, RefusesWhatCannotBeBuilt)
{
    auto build = [](const std::string& text) { return DeclaredComponentType::Build(Declare(text)); };
    EXPECT_FALSE(build(R"({ "name": "X", "fields": { "type": { "type": "int" } } })"));
    EXPECT_FALSE(build(R"({ "name": "X", "fields": { "active": { "type": "bool" } } })"));
    EXPECT_FALSE(build(R"({ "name": "X", "fields": { "1abc": { "type": "bool" } } })"));
    EXPECT_FALSE(build(R"({ "name": "X", "fields": { "has space": { "type": "bool" } } })"));

    json many = { { "name", "Many" }, { "fields", json::object() } };
    for (int i = 0; i < 70; ++i) many["fields"]["f" + std::to_string(i)] = { { "type", "int" } };
    auto tooMany = DeclaredComponentType::Build(Declare(many.dump()));
    ASSERT_FALSE(tooMany);
    EXPECT_NE(tooMany.GetError().message.find("fields"), std::string::npos);

    json big = { { "name", "Big" }, { "fields", json::object() } };
    for (int i = 0; i < 40; ++i) big["fields"]["s" + std::to_string(i)] = { { "type", "string" } };          // 40 * 32 bytes
    EXPECT_FALSE(DeclaredComponentType::Build(Declare(big.dump())));

    EXPECT_TRUE(build(R"({ "name": "Empty", "fields": {} })"));
}

TEST(DeclaredComponentManager, ADatapackComponentIsARegisteredComponentType)
{
    Shard::Tests::TestEngineContext context;        // creating a component asks the engine for its id manager
    Core::SetEngine(&context);
    PluginManager& pm = PluginManager::Get();
    pm.ResetForTests();
    TempDir dir;
    dir.Write("plugins/pack/shard-plugin.json", PluginTest::DatapackManifest("pack.health"));
    dir.Write("plugins/pack/content/components/health.json", kHealth);
    dir.Write("plugins/pack/content/components/broken.json", R"({ "name": "Broken", "fields": { "type": { "type": "int" } } })");
    PluginManagerConfig config;
    config.host = Host::Headless;
    config.registerConsoleCommands = false;
    config.searchPaths.push_back(dir.Sub("plugins"));
    ASSERT_TRUE(pm.Startup(config));

    auto& registry = Objects::Components::GetComponentRegistry();
    EXPECT_TRUE(registry.IsRegistered("Health"));
    auto made = registry.CreateComponentByName("Health");
    ASSERT_NE(made, nullptr);
    auto declared = std::dynamic_pointer_cast<DeclaredFieldsComponent>(made);
    ASSERT_NE(declared, nullptr);
    EXPECT_EQ(declared->Get("maxHp"), 100);
    EXPECT_FALSE(registry.IsRegistered("Broken"));
    ASSERT_NE(pm.Find("pack.health"), nullptr);
    EXPECT_FALSE(pm.Find("pack.health")->warnings.empty());          // why Broken was not built

    made.reset();
    declared.reset();
    pm.OnEngineDestroying();
    ASSERT_TRUE(pm.Disable("pack.health"));
    EXPECT_FALSE(registry.IsRegistered("Health"));
    pm.ResetForTests();
    Core::SetEngine(nullptr);
}

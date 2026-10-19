#include <gtest/gtest.h>

#include "engine/plugins/manifest.hpp"
#include "plugins/plugin_test_utils.hpp"

using namespace Shard::Engine::Plugins;
using nlohmann::json;

TEST(PluginManifest, ParsesAFullManifest)
{
    const json j = json::parse(R"({
        "schema": 1, "id": "com.acme.weather", "name": "Weather", "version": "1.4.2",
        "kind": "module", "moduleKind": "mod", "linkage": "dynamic", "tier": 1,
        "hosts": ["engine", "tool"],
        "engine": { "api": "1.0", "versions": ">=0.1.0 <0.3.0", "requires": ["largeWorld"] },
        "binaries": { "linux-x86_64": "bin/a.so", "windows-x86_64": "bin/a.dll" },
        "dependencies": [ "shard.audio", { "id": "com.acme.clouds", "version": "^2.1", "optional": true }, { "id": "service:weather.query" } ],
        "conflicts": [ { "id": "com.other.weather" } ],
        "provides": [ { "service": "weather.query", "version": "1.2" } ],
        "loadAfter": ["com.acme.clouds"],
        "simulation": { "affects": true, "deterministic": true },
        "content": { "mount": "data", "assetNamespace": "acme.weather", "overrides": ["materials/*"] },
        "budgets": { "memoryMB": 64, "frameMs": 1.5 },
        "permissions": ["render.feature"],
        "settings": { "rain_rate": 0.4 }
    })");
    auto parsed = ParseManifest(j);
    ASSERT_TRUE(parsed) << parsed.GetError().message;
    const PluginManifest& m = parsed.Value();
    EXPECT_EQ(m.id, "com.acme.weather");
    EXPECT_EQ(m.version.ToString(), "1.4.2");
    EXPECT_TRUE(m.IsModule());
    EXPECT_EQ(m.moduleKind, ModuleKind::Mod);
    EXPECT_EQ(m.tier, TrustTier::Native);
    EXPECT_TRUE(m.RunsOn(Host::Player));
    EXPECT_TRUE(m.RunsOn(Host::Headless));
    EXPECT_TRUE(m.RunsOn(Host::Editor));
    EXPECT_TRUE(m.RunsOn(Host::Tool));
    ASSERT_EQ(m.dependencies.size(), 3u);
    EXPECT_TRUE(m.dependencies[1].optional);
    EXPECT_EQ(m.dependencies[2].id, "service:weather.query");
    ASSERT_EQ(m.provides.size(), 1u);
    EXPECT_EQ(m.provides[0].version.ToString(), "1.2.0");
    EXPECT_EQ(m.contentMount, "data");
    EXPECT_EQ(m.Namespace(), "acme.weather");
    EXPECT_DOUBLE_EQ(m.budgetFrameMs, 1.5);
    EXPECT_TRUE(m.affectsSimulation);
    EXPECT_EQ(m.engineRequires, std::vector<std::string>{ "largeWorld" });
}

TEST(PluginManifest, EngineHostMeansEveryExecutableThatSimulates)
{
    json j = PluginTest::Manifest("a.b");
    j["hosts"] = json::array({ "engine" });
    auto m = ParseManifest(j);
    ASSERT_TRUE(m);
    EXPECT_TRUE(m.Value().RunsOn(Host::Editor));
    EXPECT_TRUE(m.Value().RunsOn(Host::Player));
    EXPECT_TRUE(m.Value().RunsOn(Host::Headless));
    EXPECT_FALSE(m.Value().RunsOn(Host::Tool));
}

TEST(PluginManifest, ReportsEveryProblemAtOnce)
{
    auto parsed = ParseManifestText(R"({ "id": "Bad Id", "version": "x", "kind": "thing", "hosts": ["moon"], "dependencies": [ { "id": "NO" } ] })");
    ASSERT_FALSE(parsed);
    const std::string& message = parsed.GetError().message;
    EXPECT_NE(message.find("not a valid id"), std::string::npos) << message;
    EXPECT_NE(message.find("semantic version"), std::string::npos) << message;
    EXPECT_NE(message.find("kind"), std::string::npos) << message;
    EXPECT_NE(message.find("unknown host"), std::string::npos) << message;
}

TEST(PluginManifest, RejectsInvalidJsonAndMissingFields)
{
    EXPECT_FALSE(ParseManifestText("not json"));
    EXPECT_FALSE(ParseManifestText("[]"));
    EXPECT_FALSE(ParseManifestText("{}"));
    EXPECT_FALSE(ParseManifestText(R"({ "id": "a.b" })"));
}

TEST(PluginManifest, ValidatesIds)
{
    EXPECT_TRUE(IsValidPluginId("com.acme.weather"));
    EXPECT_TRUE(IsValidPluginId("shard-core_2"));
    EXPECT_FALSE(IsValidPluginId(""));
    EXPECT_FALSE(IsValidPluginId("a..b"));
    EXPECT_FALSE(IsValidPluginId(".a"));
    EXPECT_FALSE(IsValidPluginId("a."));
    EXPECT_FALSE(IsValidPluginId("A.b"));
    EXPECT_FALSE(IsValidPluginId("a b"));
}

TEST(PluginManifest, DatapacksAreDataOnly)
{
    json j = PluginTest::Manifest("pack.one");
    j["kind"] = "module";
    j["moduleKind"] = "datapack";
    j["linkage"] = "dynamic";
    auto m = ParseManifest(j);
    ASSERT_TRUE(m) << m.GetError().message;
    EXPECT_EQ(m.Value().tier, TrustTier::Data);

    j["binaries"] = { { "linux-x86_64", "x.so" } };
    EXPECT_FALSE(ParseManifest(j));

    j.erase("binaries");
    j["tier"] = 1;
    EXPECT_FALSE(ParseManifest(j));

    json plain = PluginTest::Manifest("not.pack");
    plain["tier"] = 3;
    EXPECT_FALSE(ParseManifest(plain));
}

TEST(PluginManifest, PluginsDoNotNameAModuleKind)
{
    json j = PluginTest::Manifest("a.b");
    j["moduleKind"] = "code";
    EXPECT_FALSE(ParseManifest(j));
}

TEST(PluginManifest, ContentMountStaysInsideThePlugin)
{
    json j = PluginTest::Manifest("a.b");
    j["kind"] = "module";
    j["content"] = { { "mount", "../outside" } };
    EXPECT_FALSE(ParseManifest(j));
    j["content"] = { { "mount", "/etc" } };
    EXPECT_FALSE(ParseManifest(j));
}

TEST(PluginManifest, ASelfDependencyIsRefused)
{
    json j = PluginTest::Manifest("a.b");
    j["dependencies"] = json::array({ "a.b" });
    EXPECT_FALSE(ParseManifest(j));
}

TEST(PluginManifest, ModulesDefaultToCodeAndToChangingTheSimulation)
{
    json j = PluginTest::Manifest("a.b");
    j["kind"] = "module";
    auto m = ParseManifest(j);
    ASSERT_TRUE(m);
    EXPECT_EQ(m.Value().moduleKind, ModuleKind::Code);
    EXPECT_TRUE(m.Value().affectsSimulation);

    auto p = ParseManifest(PluginTest::Manifest("c.d"));
    ASSERT_TRUE(p);
    EXPECT_FALSE(p.Value().affectsSimulation);
}

TEST(PluginManifest, WritesBackWhatItReads)
{
    auto parsed = ParseManifest(PluginTest::Manifest("a.b", "2.1.0"));
    ASSERT_TRUE(parsed);
    auto round = ParseManifest(ManifestToJson(parsed.Value()));
    ASSERT_TRUE(round) << round.GetError().message;
    EXPECT_EQ(round.Value().id, "a.b");
    EXPECT_EQ(round.Value().version.ToString(), "2.1.0");
}

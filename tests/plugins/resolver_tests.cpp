#include <gtest/gtest.h>

#include "engine/plugins/resolver.hpp"
#include "plugins/plugin_test_utils.hpp"

using namespace Shard::Engine::Plugins;
using nlohmann::json;

namespace {
    PluginManifest Make(const std::string& id, const std::string& version = "1.0.0", const std::function<void(json&)>& edit = nullptr)
    {
        json j = PluginTest::Manifest(id, version);
        if (edit) edit(j);
        auto parsed = ParseManifest(j);
        EXPECT_TRUE(parsed) << id << " : " << (parsed ? "" : parsed.GetError().message);
        return parsed ? parsed.Value() : PluginManifest();
    }

    ResolveOptions Options()
    {
        ResolveOptions o;
        o.host = Host::Headless;
        o.engineVersion = SemVer{ 0, 1, 0, {} };
        return o;
    }

    std::vector<std::string> Order(const std::vector<PluginManifest>& all, const ResolveResult& r)
    {
        std::vector<std::string> ids;
        for (size_t i : r.order) ids.push_back(all[i].id);
        return ids;
    }

    const Rejection* Rejected(const ResolveResult& r, const std::string& id)
    {
        for (const Rejection& x : r.rejected) if (x.id == id) return &x;
        return nullptr;
    }
}

TEST(PluginResolver, OrdersDependenciesFirstAndTiesById)
{
    std::vector<PluginManifest> all = {
        Make("c", "1.0.0", [](json& j) { j["dependencies"] = json::array({ "b" }); }),
        Make("b", "1.0.0", [](json& j) { j["dependencies"] = json::array({ "a" }); }),
        Make("a"),
        Make("z"),
        Make("m"),
    };
    ResolveResult r = ResolvePlugins(all, Options());
    EXPECT_TRUE(r.Ok());
    EXPECT_EQ(Order(all, r), (std::vector<std::string>{ "a", "b", "c", "m", "z" }));
}

TEST(PluginResolver, OrderDoesNotDependOnTheOrderPluginsWereFound)
{
    std::vector<PluginManifest> one = { Make("x"), Make("y"), Make("w", "1.0.0", [](json& j) { j["loadAfter"] = json::array({ "y" }); }) };
    std::vector<PluginManifest> two = { one[2], one[1], one[0] };
    EXPECT_EQ(Order(one, ResolvePlugins(one, Options())), Order(two, ResolvePlugins(two, Options())));
}

TEST(PluginResolver, LoadAfterAndLoadBefore)
{
    std::vector<PluginManifest> all = {
        Make("a", "1.0.0", [](json& j) { j["loadAfter"] = json::array({ "z" }); }),
        Make("b", "1.0.0", [](json& j) { j["loadBefore"] = json::array({ "a", "z" }); }),
        Make("z"),
    };
    ResolveResult r = ResolvePlugins(all, Options());
    EXPECT_EQ(Order(all, r), (std::vector<std::string>{ "b", "z", "a" }));
}

TEST(PluginResolver, DropsWhatDependsOnAMissingPluginAndCascades)
{
    std::vector<PluginManifest> all = {
        Make("a", "1.0.0", [](json& j) { j["dependencies"] = json::array({ "gone" }); }),
        Make("b", "1.0.0", [](json& j) { j["dependencies"] = json::array({ "a" }); }),
        Make("c"),
    };
    ResolveResult r = ResolvePlugins(all, Options());
    EXPECT_EQ(Order(all, r), std::vector<std::string>{ "c" });
    ASSERT_TRUE(Rejected(r, "a"));
    EXPECT_EQ(Rejected(r, "a")->kind, Rejection::Kind::MissingDependency);
    ASSERT_TRUE(Rejected(r, "b"));
    EXPECT_EQ(Rejected(r, "b")->kind, Rejection::Kind::MissingDependency);
}

TEST(PluginResolver, AnOptionalDependencyMayBeAbsent)
{
    std::vector<PluginManifest> all = {
        Make("a", "1.0.0", [](json& j) { j["dependencies"] = json::array({ json{ { "id", "maybe" }, { "optional", true } } }); }),
    };
    ResolveResult r = ResolvePlugins(all, Options());
    EXPECT_EQ(Order(all, r), std::vector<std::string>{ "a" });

    // present, it loads first
    all.push_back(Make("maybe"));
    r = ResolvePlugins(all, Options());
    EXPECT_EQ(Order(all, r), (std::vector<std::string>{ "maybe", "a" }));
}

TEST(PluginResolver, ChecksTheVersionOfADependency)
{
    std::vector<PluginManifest> all = {
        Make("a", "1.0.0", [](json& j) { j["dependencies"] = json::array({ json{ { "id", "lib" }, { "version", "^2.0" } } }); }),
        Make("lib", "1.5.0"),
    };
    ResolveResult r = ResolvePlugins(all, Options());
    EXPECT_TRUE(Rejected(r, "a"));
    EXPECT_EQ(Order(all, r), std::vector<std::string>{ "lib" });
}

TEST(PluginResolver, ServicesMeetServiceDependencies)
{
    std::vector<PluginManifest> all = {
        Make("user", "1.0.0", [](json& j) { j["dependencies"] = json::array({ "service:weather.query" }); }),
        Make("zzz.provider", "1.0.0", [](json& j) { j["provides"] = json::array({ json{ { "service", "weather.query" }, { "version", "1.0" } } }); }),
    };
    ResolveResult r = ResolvePlugins(all, Options());
    EXPECT_EQ(Order(all, r), (std::vector<std::string>{ "zzz.provider", "user" }));
    ASSERT_EQ(r.services.size(), 1u);
    EXPECT_EQ(r.services[0].second, "zzz.provider");

    all.pop_back();
    r = ResolvePlugins(all, Options());
    EXPECT_TRUE(Rejected(r, "user"));
}

TEST(PluginResolver, ConflictsDropTheLastById)
{
    std::vector<PluginManifest> all = {
        Make("a.weather", "1.0.0", [](json& j) { j["conflicts"] = json::array({ "b.weather" }); }),
        Make("b.weather"),
        Make("other"),
    };
    ResolveResult r = ResolvePlugins(all, Options());
    EXPECT_EQ(Order(all, r), (std::vector<std::string>{ "a.weather", "other" }));
    ASSERT_TRUE(Rejected(r, "b.weather"));
    EXPECT_EQ(Rejected(r, "b.weather")->kind, Rejection::Kind::Conflict);
}

TEST(PluginResolver, TheProjectsRequiredPluginWinsAConflict)
{
    std::vector<PluginManifest> all = {
        Make("a.weather", "1.0.0", [](json& j) { j["conflicts"] = json::array({ "b.weather" }); }),
        Make("b.weather"),
    };
    ResolveOptions o = Options();
    o.required.insert("b.weather");
    ResolveResult r = ResolvePlugins(all, o);
    EXPECT_EQ(Order(all, r), std::vector<std::string>{ "b.weather" });
    EXPECT_TRUE(r.Ok());
}

TEST(PluginResolver, ARequiredPluginThatCannotLoadIsAnError)
{
    std::vector<PluginManifest> all = { Make("needs.gone", "1.0.0", [](json& j) { j["dependencies"] = json::array({ "gone" }); }) };
    ResolveOptions o = Options();
    o.required.insert("needs.gone");
    ResolveResult r = ResolvePlugins(all, o);
    EXPECT_FALSE(r.Ok());
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_NE(r.errors[0].find("needs.gone"), std::string::npos);
}

TEST(PluginResolver, ReportsACycle)
{
    std::vector<PluginManifest> all = {
        Make("a", "1.0.0", [](json& j) { j["dependencies"] = json::array({ "b" }); }),
        Make("b", "1.0.0", [](json& j) { j["dependencies"] = json::array({ "c" }); }),
        Make("c", "1.0.0", [](json& j) { j["dependencies"] = json::array({ "a" }); }),
        Make("free"),
    };
    ResolveResult r = ResolvePlugins(all, Options());
    EXPECT_EQ(Order(all, r), std::vector<std::string>{ "free" });
    ASSERT_TRUE(Rejected(r, "a"));
    EXPECT_EQ(Rejected(r, "a")->kind, Rejection::Kind::Cycle);
    EXPECT_NE(Rejected(r, "a")->reason.find("->"), std::string::npos);
}

TEST(PluginResolver, FiltersByHostEngineVersionApiAndFeatures)
{
    std::vector<PluginManifest> all = {
        Make("editor.only", "1.0.0", [](json& j) { j["hosts"] = json::array({ "editor" }); }),
        Make("old.engine", "1.0.0", [](json& j) { j["engine"] = { { "versions", "<0.1.0" } }; }),
        Make("new.api", "1.0.0", [](json& j) { j["engine"] = { { "api", "9.0" } }; }),
        Make("needs.feature", "1.0.0", [](json& j) { j["engine"] = { { "requires", json::array({ "teleportation" }) } }; }),
        Make("fine"),
    };
    ResolveResult r = ResolvePlugins(all, Options());
    EXPECT_EQ(Order(all, r), std::vector<std::string>{ "fine" });
    EXPECT_EQ(Rejected(r, "editor.only")->kind, Rejection::Kind::WrongHost);
    EXPECT_EQ(Rejected(r, "old.engine")->kind, Rejection::Kind::Incompatible);
    EXPECT_EQ(Rejected(r, "new.api")->kind, Rejection::Kind::Incompatible);
    EXPECT_EQ(Rejected(r, "needs.feature")->kind, Rejection::Kind::Incompatible);

    ResolveOptions o = Options();
    o.engineFeatures.insert("teleportation");
    r = ResolvePlugins(all, o);
    EXPECT_FALSE(Rejected(r, "needs.feature"));
}

TEST(PluginResolver, KeepsTheHighestVersionOfTheSamePlugin)
{
    std::vector<PluginManifest> all = { Make("p", "1.0.0"), Make("p", "1.2.0"), Make("p", "1.1.0") };
    ResolveResult r = ResolvePlugins(all, Options());
    ASSERT_EQ(r.order.size(), 1u);
    EXPECT_EQ(all[r.order[0]].version.ToString(), "1.2.0");
    EXPECT_EQ(r.rejected.size(), 2u);
}

TEST(PluginResolver, TheProjectsRangePicksTheVersion)
{
    std::vector<PluginManifest> all = { Make("p", "1.0.0"), Make("p", "2.0.0") };
    ResolveOptions o = Options();
    o.projectRanges.emplace_back("p", *VersionRange::Parse("^1.0"));
    ResolveResult r = ResolvePlugins(all, o);
    ASSERT_EQ(r.order.size(), 1u);
    EXPECT_EQ(all[r.order[0]].version.ToString(), "1.0.0");

    // nothing fits : it is reported, not silently loaded
    o.projectRanges.clear();
    o.projectRanges.emplace_back("p", *VersionRange::Parse("^3.0"));
    r = ResolvePlugins(all, o);
    EXPECT_TRUE(r.order.empty());
}

TEST(PluginResolver, ProjectCanTurnAPluginOff)
{
    std::vector<PluginManifest> all = { Make("a"), Make("b", "1.0.0", [](json& j) { j["dependencies"] = json::array({ "a" }); }) };
    ResolveOptions o = Options();
    o.disabled.insert("a");
    ResolveResult r = ResolvePlugins(all, o);
    EXPECT_TRUE(r.order.empty());
    EXPECT_EQ(Rejected(r, "a")->kind, Rejection::Kind::Disabled);
    EXPECT_EQ(Rejected(r, "b")->kind, Rejection::Kind::MissingDependency);
}

TEST(PluginResolver, TheEnginesModulesLoadBeforeEverythingElse)
{
    std::vector<PluginManifest> all = {
        Make("aaa.game", "1.0.0", [](json& j) { j["kind"] = "module"; j["moduleKind"] = "code"; }),
        Make("shard.physics", "1.0.0", [](json& j) { j["kind"] = "module"; j["moduleKind"] = "engine"; }),
        Make("shard.audio", "1.0.0", [](json& j) { j["kind"] = "module"; j["moduleKind"] = "engine"; }),
        Make("bbb.plugin"),
    };
    ResolveResult r = ResolvePlugins(all, Options());
    EXPECT_EQ(Order(all, r), (std::vector<std::string>{ "shard.audio", "shard.physics", "aaa.game", "bbb.plugin" }));
}

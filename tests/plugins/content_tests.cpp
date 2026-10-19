#include <gtest/gtest.h>

#include "engine/plugins/content.hpp"
#include "plugins/plugin_test_utils.hpp"

using namespace Shard::Engine::Plugins;
using nlohmann::json;
using PluginTest::TempDir;

namespace {
    PluginManifest ModuleAt(const TempDir& dir, const std::string& folder, const std::string& id, const std::vector<std::string>& overrides = {})
    {
        json j = PluginTest::Manifest(id);
        j["kind"] = "module";
        j["moduleKind"] = "mod";
        j["content"] = { { "mount", "content" }, { "overrides", overrides } };
        PluginManifest m = ParseManifest(j).Value();
        m.rootDir = dir.Sub(folder);
        return m;
    }
}

TEST(ContentMounts, ModUrisParseAndBuild)
{
    std::string id, path;
    EXPECT_TRUE(ContentMounts::ParseModUri("mod://com.acme.x/materials/a.mat", id, path));
    EXPECT_EQ(id, "com.acme.x");
    EXPECT_EQ(path, "materials/a.mat");
    EXPECT_FALSE(ContentMounts::ParseModUri("file://x/y", id, path));
    EXPECT_FALSE(ContentMounts::ParseModUri("mod://onlyid", id, path));
    EXPECT_FALSE(ContentMounts::ParseModUri("mod:///x", id, path));
    EXPECT_EQ(ContentMounts::MakeModUri("a.b", "/c/d.txt"), "mod://a.b/c/d.txt");
    EXPECT_TRUE(ContentMounts::IsModUri("mod://a/b"));
}

TEST(ContentMounts, ResolvesReadsAndListsAModulesFiles)
{
    TempDir dir;
    dir.Write("one/content/materials/stone.mat", "stone");
    dir.Write("one/content/tables/items.json", "{}");
    ContentMounts mounts;
    std::string err;
    ASSERT_TRUE(mounts.Mount(ModuleAt(dir, "one", "mod.one"), 1, &err)) << err;

    EXPECT_TRUE(mounts.Exists("mod://mod.one/materials/stone.mat"));
    EXPECT_FALSE(mounts.Exists("mod://mod.one/materials/missing.mat"));
    EXPECT_FALSE(mounts.Exists("mod://nobody/materials/stone.mat"));
    EXPECT_EQ(*mounts.ReadText("mod://mod.one/materials/stone.mat"), "stone");
    EXPECT_EQ(mounts.List("mod.one"), (std::vector<std::string>{ "materials/stone.mat", "tables/items.json" }));
    EXPECT_EQ(mounts.List("mod.one", "tables"), (std::vector<std::string>{ "tables/items.json" }));
}

TEST(ContentMounts, NeverLeavesTheMountsFolder)
{
    TempDir dir;
    dir.Write("one/content/a.txt", "a");
    dir.Write("one/secret.txt", "secret");
    ContentMounts mounts;
    ASSERT_TRUE(mounts.Mount(ModuleAt(dir, "one", "mod.one"), 1));
    EXPECT_FALSE(mounts.Exists("mod://mod.one/../secret.txt"));
    EXPECT_FALSE(mounts.Exists("mod://mod.one/sub/../../secret.txt"));
    EXPECT_FALSE(mounts.ReadText("mod://mod.one/../secret.txt"));
    EXPECT_FALSE(mounts.ResolveLogical("../secret.txt"));
    EXPECT_TRUE(mounts.List("mod.one", "../").empty());
}

TEST(ContentMounts, LaterLayersOnlyReplaceWhatTheyOverride)
{
    TempDir dir;
    dir.Write("a/content/materials/stone.mat", "from a");
    dir.Write("a/content/materials/wood.mat", "from a");
    dir.Write("b/content/materials/stone.mat", "from b");
    dir.Write("b/content/materials/wood.mat", "from b");
    dir.Write("b/content/materials/new.mat", "from b");
    ContentMounts mounts;
    ASSERT_TRUE(mounts.Mount(ModuleAt(dir, "a", "mod.a"), 1));
    ASSERT_TRUE(mounts.Mount(ModuleAt(dir, "b", "mod.b", { "materials/wood.mat" }), 2));

    EXPECT_EQ(mounts.ResolveLogical("materials/stone.mat")->pluginId, "mod.a");          // b did not say it replaces it
    EXPECT_EQ(mounts.ResolveLogical("materials/wood.mat")->pluginId, "mod.b");           // b did
    EXPECT_EQ(mounts.ResolveLogical("materials/new.mat")->pluginId, "mod.b");
    EXPECT_FALSE(mounts.ResolveLogical("materials/none.mat"));

    const auto conflicts = mounts.FindConflicts();
    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts[0].logicalPath, "materials/stone.mat");
    EXPECT_EQ(conflicts[0].keptFrom, "mod.a");
    EXPECT_EQ(conflicts[0].rejectedFrom, "mod.b");
    EXPECT_EQ(mounts.ListLogical("materials").size(), 3u);
}

TEST(ContentMounts, WildcardOverrides)
{
    TempDir dir;
    dir.Write("a/content/materials/stone.mat", "a");
    dir.Write("a/content/sounds/boom.wav", "a");
    dir.Write("b/content/materials/stone.mat", "b");
    dir.Write("b/content/sounds/boom.wav", "b");
    ContentMounts mounts;
    ASSERT_TRUE(mounts.Mount(ModuleAt(dir, "a", "mod.a"), 1));
    ASSERT_TRUE(mounts.Mount(ModuleAt(dir, "b", "mod.b", { "materials/*" }), 2));
    EXPECT_EQ(mounts.ResolveLogical("materials/stone.mat")->pluginId, "mod.b");
    EXPECT_EQ(mounts.ResolveLogical("sounds/boom.wav")->pluginId, "mod.a");

    ContentMounts all;
    ASSERT_TRUE(all.Mount(ModuleAt(dir, "a", "mod.a"), 1));
    ASSERT_TRUE(all.Mount(ModuleAt(dir, "b", "mod.b", { "*" }), 2));
    EXPECT_TRUE(all.FindConflicts().empty());
    EXPECT_EQ(all.ResolveLogical("sounds/boom.wav")->pluginId, "mod.b");
}

TEST(ContentMounts, UnmountRemovesAnOwnersLayers)
{
    TempDir dir;
    dir.Write("a/content/x.txt", "a");
    dir.Write("b/content/x.txt", "b");
    ContentMounts mounts;
    mounts.Mount(ModuleAt(dir, "a", "mod.a"), 1);
    mounts.Mount(ModuleAt(dir, "b", "mod.b", { "*" }), 2);
    EXPECT_EQ(mounts.ResolveLogical("x.txt")->pluginId, "mod.b");
    EXPECT_EQ(mounts.Unmount(2), 1u);
    EXPECT_EQ(mounts.ResolveLogical("x.txt")->pluginId, "mod.a");
    EXPECT_FALSE(mounts.Exists("mod://mod.b/x.txt"));
}

TEST(ContentMounts, AMissingContentFolderOnlyMattersToDatapacksAndMods)
{
    TempDir dir;
    ContentMounts mounts;
    EXPECT_FALSE(mounts.Mount(ModuleAt(dir, "nothing", "mod.none"), 1));

    json j = PluginTest::Manifest("code.mod");
    j["kind"] = "module";
    j["moduleKind"] = "code";
    j["content"] = { { "mount", "content" } };
    PluginManifest code = ParseManifest(j).Value();
    code.rootDir = dir.Sub("nothing");
    EXPECT_TRUE(mounts.Mount(code, 2));
    EXPECT_EQ(mounts.Find("code.mod"), nullptr);
}

TEST(ContentMounts, AssetGuidsAreStableAndDistinct)
{
    const auto a1 = ContentMounts::AssetGuid("acme.weather", "materials/rain.mat");
    const auto a2 = ContentMounts::AssetGuid("acme.weather", "materials\\rain.mat");
    const auto a3 = ContentMounts::AssetGuid("acme.weather", "Materials/Rain.mat");
    const auto b = ContentMounts::AssetGuid("acme.weather", "materials/snow.mat");
    const auto c = ContentMounts::AssetGuid("other.ns", "materials/rain.mat");
    EXPECT_FALSE(a1.IsNull());
    EXPECT_EQ(a1, a2);              // separators do not matter
    EXPECT_EQ(a1, a3);              // nor does case : Windows and Linux must agree
    EXPECT_NE(a1, b);
    EXPECT_NE(a1, c);
    EXPECT_EQ(a1, ContentMounts::AssetGuid("acme.weather", "materials/rain.mat"));
}

TEST(ContentMounts, TablesMergeKeyByKeyInLayerOrder)
{
    TempDir dir;
    dir.Write("a/content/tables/weapons.json", R"({ "sword": { "damage": 5, "weight": 2 }, "bow": { "damage": 3 } })");
    dir.Write("b/content/tables/weapons.json", R"({ "sword": { "damage": 9 }, "axe": { "damage": 7 } })");
    ContentMounts mounts;
    mounts.Mount(ModuleAt(dir, "a", "mod.a"), 1);
    mounts.Mount(ModuleAt(dir, "b", "mod.b"), 2);
    const json t = mounts.LoadTable("weapons");
    EXPECT_EQ(t["sword"]["damage"], 9);
    EXPECT_EQ(t["sword"]["weight"], 2);
    EXPECT_EQ(t["bow"]["damage"], 3);
    EXPECT_EQ(t["axe"]["damage"], 7);
    EXPECT_EQ(mounts.TableNames(), std::vector<std::string>{ "weapons" });
    EXPECT_TRUE(mounts.LoadTable("none").empty());
    EXPECT_TRUE(mounts.FindConflicts().empty());          // tables merge on purpose
}

TEST(ContentMounts, TranslationsFlatOrNestedWithFallbacks)
{
    TempDir dir;
    dir.Write("a/content/lang/fr.json", R"({ "ui.start": "Jouer", "ui": { "quit": "Quitter" } })");
    dir.Write("b/content/lang/fr.json", R"({ "ui.start": "Commencer" })");
    ContentMounts mounts;
    mounts.Mount(ModuleAt(dir, "a", "mod.a"), 1);
    EXPECT_EQ(mounts.Translate("ui.start", "fr"), "Jouer");
    EXPECT_EQ(mounts.Translate("ui.quit", "fr"), "Quitter");
    mounts.Mount(ModuleAt(dir, "b", "mod.b"), 2);
    EXPECT_EQ(mounts.Translate("ui.start", "fr"), "Commencer");
    EXPECT_EQ(mounts.Translate("ui.missing", "fr"), "ui.missing");
    EXPECT_EQ(mounts.Translate("ui.start", "de"), "ui.start");
    EXPECT_EQ(mounts.Translate("ui.start", "de", "Start"), "Start");
}

// ---- Validation ----

TEST(ContentValidation, AcceptsGoodContentAndUnknownFolders)
{
    TempDir dir;
    dir.Write("c/worlds/a.world", R"({ "name": "a", "actors": [ { "name": "Box", "components": [ { "type": "transform" } ], "children": [ { "name": "Lid" } ] } ], "settings": {} })");
    dir.Write("c/prefabs/door.json", R"({ "name": "Door", "components": [ { "type": "transform" } ] })");
    dir.Write("c/tables/t.json", "{}");
    dir.Write("c/lang/en.json", R"({ "a": "b", "n": { "c": "d" } })");
    dir.Write("c/patches/p.patch", R"({ "world": "a.world", "ops": [ { "op": "setSettings", "settings": {} } ] })");
    dir.Write("c/components/health.json", R"({ "name": "Health", "fields": { "hp": { "type": "float", "default": 100.0 } } })");
    dir.Write("c/textures/whatever.png", "not looked at");
    dir.Write("c/game/anything.bin", "free form");
    EXPECT_TRUE(ValidateContentFolder(dir.Sub("c")).empty());
}

TEST(ContentValidation, ReportsEachProblemWithItsFile)
{
    TempDir dir;
    dir.Write("c/worlds/broken.world", "{ not json");
    dir.Write("c/worlds/noname.world", R"({ "actors": [ { "components": [] } ] })");
    dir.Write("c/tables/array.json", "[]");
    dir.Write("c/lang/bad.json", R"({ "a": 5 })");
    dir.Write("c/patches/noworld.patch", R"({ "ops": [] })");
    dir.Write("c/patches/badop.patch", R"({ "world": "a.world", "ops": [ { "op": "explode" } ] })");
    dir.Write("c/components/badtype.json", R"({ "name": "X", "fields": { "a": { "type": "quaternion" } } })");
    dir.Write("c/prefabs/nameless.json", R"({ "components": [] })");
    const auto problems = ValidateContentFolder(dir.Sub("c"));
    auto has = [&](const std::string& needle) {
        for (const auto& p : problems) if (p.find(needle) != std::string::npos) return true;
        return false;
    };
    EXPECT_TRUE(has("worlds/broken.world")) ;
    EXPECT_TRUE(has("worlds/noname.world"));
    EXPECT_TRUE(has("tables/array.json"));
    EXPECT_TRUE(has("lang/bad.json"));
    EXPECT_TRUE(has("patches/noworld.patch"));
    EXPECT_TRUE(has("patches/badop.patch"));
    EXPECT_TRUE(has("components/badtype.json"));
    EXPECT_TRUE(has("prefabs/nameless.json"));
    EXPECT_FALSE(ValidateContentFolder(dir.Sub("absent")).empty());
}

TEST(DeclaredComponents, ParseFieldsAndDefaults)
{
    auto ok = ParseDeclaredComponent(json::parse(R"({ "name": "Health", "fields": {
        "hp": { "type": "float", "default": 80 }, "alive": { "type": "bool" }, "tint": { "type": "color" }, "pos": { "type": "vec3", "default": [1, 2, 3] } } })"));
    ASSERT_TRUE(ok) << ok.GetError().message;
    EXPECT_EQ(ok.Value().name, "Health");
    ASSERT_EQ(ok.Value().fields.size(), 4u);
    for (const auto& f : ok.Value().fields) {
        if (f.name == "alive") { EXPECT_EQ(f.defaultValue, false); }
        if (f.name == "tint") { EXPECT_EQ(f.defaultValue.size(), 4u); }
        if (f.name == "hp") { EXPECT_EQ(f.defaultValue, 80); }
    }
    EXPECT_FALSE(ParseDeclaredComponent(json::parse(R"({ "name": "X", "fields": { "a": { "type": "int", "default": "no" } } })")));
    EXPECT_FALSE(ParseDeclaredComponent(json::parse(R"({ "fields": {} })")));
    EXPECT_FALSE(ParseDeclaredComponent(json::parse(R"({ "name": "X" })")));
}

// ---- World patches ----

namespace {
    json SampleWorld()
    {
        return json::parse(R"({
            "name": "room",
            "actors": [
                { "name": "Table", "components": [ { "type": "transform", "position": [0, 0, 0] }, { "type": "light", "intensity": 1.0 } ],
                  "children": [ { "name": "Vase", "components": [] } ] },
                { "name": "Chair", "components": [] }
            ],
            "settings": { "ambient_color": [0.1, 0.1, 0.1], "ssao": { "enabled": true, "radius": 0.5 } }
        })");
    }
}

TEST(WorldPatch, AddsAndRemovesActors)
{
    json w = SampleWorld();
    auto r = ApplyWorldPatch(w, json::parse(R"({ "world": "room.world", "ops": [
        { "op": "addActor", "actor": { "name": "Lamp", "components": [] } },
        { "op": "addActor", "parent": "Table", "actor": { "name": "Cup" } },
        { "op": "removeActor", "path": "Chair" },
        { "op": "removeActor", "path": "Table/Vase" } ] })"));
    ASSERT_TRUE(r) << r.GetError().message;
    ASSERT_EQ(w["actors"].size(), 2u);
    EXPECT_EQ(w["actors"][0]["name"], "Table");
    EXPECT_EQ(w["actors"][1]["name"], "Lamp");
    ASSERT_EQ(w["actors"][0]["children"].size(), 1u);
    EXPECT_EQ(w["actors"][0]["children"][0]["name"], "Cup");
}

TEST(WorldPatch, ChangesComponentsAndSettings)
{
    json w = SampleWorld();
    auto r = ApplyWorldPatch(w, json::parse(R"({ "ops": [
        { "op": "setComponent", "path": "Table", "type": "light", "fields": { "intensity": 3.5, "range": 10 } },
        { "op": "addComponent", "path": "Chair", "component": { "type": "audio_source" } },
        { "op": "removeComponent", "path": "Table", "type": "transform" },
        { "op": "setSettings", "settings": { "ssao": { "radius": 1.5 }, "fog": true } } ] })"));
    ASSERT_TRUE(r) << r.GetError().message;
    EXPECT_DOUBLE_EQ(w["actors"][0]["components"][0]["intensity"].get<double>(), 3.5);
    EXPECT_EQ(w["actors"][0]["components"][0]["range"], 10);
    EXPECT_EQ(w["actors"][0]["components"].size(), 1u);
    EXPECT_EQ(w["actors"][1]["components"][0]["type"], "audio_source");
    EXPECT_DOUBLE_EQ(w["settings"]["ssao"]["radius"].get<double>(), 1.5);
    EXPECT_EQ(w["settings"]["ssao"]["enabled"], true);          // merged, not replaced
    EXPECT_EQ(w["settings"]["fog"], true);
}

TEST(WorldPatch, AllOrNothing)
{
    json w = SampleWorld();
    const json before = w;
    auto r = ApplyWorldPatch(w, json::parse(R"({ "ops": [
        { "op": "addActor", "actor": { "name": "Fine" } },
        { "op": "removeActor", "path": "Nobody" } ] })"));
    ASSERT_FALSE(r);
    EXPECT_NE(r.GetError().message.find("op 1"), std::string::npos);
    EXPECT_EQ(w, before);
}

TEST(WorldPatch, RefusesWhatIsMalformed)
{
    json w = SampleWorld();
    EXPECT_FALSE(ApplyWorldPatch(w, json::parse(R"({ "ops": [ { "op": "addActor", "actor": { "name": "Table" } } ] })")));          // duplicate name
    EXPECT_FALSE(ApplyWorldPatch(w, json::parse(R"({ "ops": [ { "op": "setComponent", "path": "Table", "type": "ghost", "fields": {} } ] })")));
    EXPECT_FALSE(ApplyWorldPatch(w, json::parse(R"({ "ops": [ { "op": "nope" } ] })")));
    EXPECT_FALSE(ApplyWorldPatch(w, json::parse(R"({ "ops": [ { "op": "addActor", "parent": "Ghost", "actor": { "name": "X" } } ] })")));
    EXPECT_FALSE(ApplyWorldPatch(w, json::parse(R"({ "nothing": 1 })")));
}

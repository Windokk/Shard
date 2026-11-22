#include <gtest/gtest.h>

#include "engine/assets/serialization/collision/collision_json.hpp"
#include "engine/assets/assetID.hpp"
#include "engine/assets/project/project.hpp"
#include "engine/assets/serialization/project/project_serializer.hpp"
#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/tags/tag_registry.hpp"

#include <filesystem>
#include <fstream>

using namespace Shard::Engine;
namespace Col = Shard::Engine::Collision;

class CollisionJson : public ::testing::Test {
    protected:
        void SetUp() override {
            Core::TagRegistry::Get().Clear();
            Core::TagRegistry::Get().SetDeclarations(Core::TagSource::Project, "", { { "Enemy", "" }, { "World", "" }, { "Prop", "" }, { "Surface.Metal", "" } });
        }
        void TearDown() override { Core::TagRegistry::Get().Clear(); }
};

TEST_F(CollisionJson, ProfileRoundTrips) {
    const nlohmann::json source = nlohmann::json::parse(R"({
        "base": "Character",
        "identity": ["Enemy"],
        "rules": [
            { "target": "World | Prop", "response": "Block" },
            { "target": "Enemy", "response": "Ignore" }
        ],
        "default": "Overlap",
        "mode": "Solid",
        "material": "Steel",
        "events": { "overlaps": false, "hits": true, "minImpactSpeed": 2.5 }
    })");
    std::vector<std::string> problems;
    const Col::Profile profile = Serialization::ProfileFromJson("Skeleton", source, &problems);
    EXPECT_TRUE(problems.empty());
    EXPECT_EQ(profile.base, "Character");
    ASSERT_EQ(profile.rules.size(), 2u);
    EXPECT_EQ(profile.rules[0].response, Col::Response::Block);
    EXPECT_EQ(*profile.defaultResponse, Col::Response::Overlap);
    EXPECT_EQ(*profile.mode, Col::Mode::Solid);
    EXPECT_FLOAT_EQ(profile.events->minImpactSpeed, 2.5f);
    EXPECT_FALSE(profile.events->overlaps);

    const nlohmann::ordered_json again = Serialization::ProfileToJson(profile);
    const Col::Profile second = Serialization::ProfileFromJson("Skeleton", again);
    EXPECT_EQ(Serialization::ProfileToJson(second).dump(), again.dump());
    EXPECT_EQ(second.rules[0].target, profile.rules[0].target);
}

TEST_F(CollisionJson, ABadRuleIsReportedAndSkipped) {
    const nlohmann::json source = nlohmann::json::parse(R"({
        "rules": [
            { "target": "World", "response": "Smash" },
            { "target": "Enemy &", "response": "Block" },
            { "target": "Prop", "response": "Block" }
        ],
        "mode": "Floating"
    })");
    std::vector<std::string> problems;
    const Col::Profile profile = Serialization::ProfileFromJson("Odd", source, &problems);
    EXPECT_GE(problems.size(), 2u);
    EXPECT_FALSE(profile.mode.has_value());
    EXPECT_FALSE(profile.rules.empty());
}

TEST_F(CollisionJson, MaterialRoundTrips) {
    const nlohmann::json source = nlohmann::json::parse(R"({ "friction": 0.8, "restitution": 0.1, "density": 7800, "combine": "Max", "surface": "Surface.Metal" })");
    const Col::PhysicsMaterial material = Serialization::MaterialFromJson("Steel", source);
    EXPECT_FLOAT_EQ(material.friction, 0.8f);
    EXPECT_EQ(material.combine, Col::Combine::Max);
    EXPECT_EQ(material.surface, "Surface.Metal");
    const Col::PhysicsMaterial again = Serialization::MaterialFromJson("Steel", Serialization::MaterialToJson(material));
    EXPECT_FLOAT_EQ(again.density, 7800.0f);
    EXPECT_EQ(again.combine, Col::Combine::Max);
}

TEST_F(CollisionJson, InteractionRowKeepsItsActions) {
    const nlohmann::json source = nlohmann::json::parse(R"({
        "name": "shot hits enemy", "a": "Prop", "b": "Enemy", "event": "Hit", "minSpeed": 3,
        "actions": [ { "type": "Destroy", "target": "self" }, { "type": "ApplyImpulse", "strength": 4.5 } ]
    })");
    std::vector<std::string> problems;
    const Col::InteractionRow row = Serialization::InteractionFromJson(source, &problems);
    EXPECT_TRUE(problems.empty());
    EXPECT_EQ(row.event, Col::EventKind::Hit);
    EXPECT_FLOAT_EQ(row.minSpeed, 3.0f);
    ASSERT_EQ(row.actions.size(), 2u);
    EXPECT_EQ(row.actions[0].type, "Destroy");
    EXPECT_FLOAT_EQ(row.actions[1].ParamFloat("strength", 0.0f), 4.5f);

    const Col::InteractionRow again = Serialization::InteractionFromJson(Serialization::InteractionToJson(row));
    EXPECT_EQ(again.name, row.name);
    EXPECT_EQ(again.actions.size(), 2u);
    EXPECT_EQ(again.a, row.a);
}

namespace {
    void AddAsset(Filesystem::AssetIDManager& ids, int id, const std::string& name, const std::filesystem::path& file)
    {
        auto info = std::make_shared<Filesystem::AssetInfos>();
        info->baseInfos.path = Filesystem::Path(file.generic_string());
        info->baseInfos.nameInProject = name;
        info->baseInfos.type = Filesystem::Path(name).GetExtensionType();
        ids.AssignID(Filesystem::AssetID(id), info);
    }

    void WriteText(const std::filesystem::path& file, const std::string& text)
    {
        std::ofstream(file) << text;
    }
}

TEST_F(CollisionJson, PhysicsMaterialAssetsLoadByProjectPath) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "shard_physmat_test";
    std::filesystem::create_directories(dir);
    WriteText(dir / "steel.physmat", R"({ "friction": 0.8, "density": 7800, "combine": "Max", "surface": "Surface.Metal" })");
    WriteText(dir / "broken.physmat", "{ not json");
    WriteText(dir / "other.txt", R"({ "friction": 9 })");

    Filesystem::AssetIDManager ids;
    ids.InitCounter(Filesystem::kFirstProjectAssetID);
    AddAsset(ids, Filesystem::kFirstProjectAssetID, "physics/steel.physmat", dir / "steel.physmat");
    AddAsset(ids, Filesystem::kFirstProjectAssetID + 1, "physics/broken.physmat", dir / "broken.physmat");
    AddAsset(ids, Filesystem::kFirstProjectAssetID + 2, "physics/other.txt", dir / "other.txt");
    AddAsset(ids, 10, "engine/builtin.physmat", dir / "steel.physmat");

    std::vector<std::string> problems;
    const auto materials = Serialization::LoadPhysicsMaterialAssets(ids, &problems);
    std::filesystem::remove_all(dir);

    ASSERT_EQ(materials.size(), 1u);
    EXPECT_EQ(materials[0].name, "physics/steel.physmat");
    EXPECT_FLOAT_EQ(materials[0].friction, 0.8f);
    EXPECT_EQ(materials[0].combine, Col::Combine::Max);
    EXPECT_EQ(problems.size(), 1u);

    Col::ProfileRegistry::Get().SetMaterials(Col::Source::Project, "", materials);
    ASSERT_NE(Col::ProfileRegistry::Get().FindMaterial("physics/steel.physmat"), nullptr);
    EXPECT_EQ(Col::ProfileRegistry::Get().FindMaterial("steel"), nullptr);
    Col::ProfileRegistry::Get().SetMaterials(Col::Source::Project, "", {});
}

TEST_F(CollisionJson, RenamingAPhysicsMaterialFollowsInProfiles) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "shard_physmat_rename";
    std::filesystem::create_directories(dir);
    const std::string file = (dir / "project.json").generic_string();

    Projects::Project project("test", Filesystem::Path(dir.generic_string()), Filesystem::Path(dir.generic_string()), Filesystem::Path(dir.generic_string()), Projects::BuildSettings(), Filesystem::Path((dir / "db.json").generic_string()));
    project.versionMajor = project.versionMinor = project.versionPatch = 0;
    for (const char* material : { "physics/steel.physmat", "physics/rubber.physmat", "other/wood.physmat", "" }) {
        Col::Profile profile;
        profile.name = std::string("P") + std::to_string(project.GetCollisionSettings()->profiles.size());
        profile.material = material;
        project.GetCollisionSettings()->profiles.push_back(profile);
    }
    project.MarkSaved();
    ASSERT_TRUE(Filesystem::Path(file).WriteFile(Serialization::ProjectToJson(&project).dump()));

    project.RemapPhysicsMaterials("physics/steel.physmat", "physics/metal/steel.physmat", false, file);
    project.RemapPhysicsMaterials("physics", "surfaces", true, file);
    project.RemapPhysicsMaterials("other/wood.physmat", "", false, file);

    const auto& profiles = project.GetCollisionSettings()->profiles;
    EXPECT_EQ(profiles[0].material, "surfaces/metal/steel.physmat");
    EXPECT_EQ(profiles[1].material, "surfaces/rubber.physmat");
    EXPECT_EQ(profiles[2].material, "other/wood.physmat");
    EXPECT_EQ(profiles[3].material, "");

    const nlohmann::json written = nlohmann::json::parse(Filesystem::Path(file).ReadFile());
    EXPECT_EQ(written["collision_profiles"]["P0"]["material"], "surfaces/metal/steel.physmat");
    EXPECT_EQ(written["collision_profiles"]["P1"]["material"], "surfaces/rubber.physmat");
    EXPECT_FALSE(project.HasUnsavedChanges());
    std::filesystem::remove_all(dir);
}

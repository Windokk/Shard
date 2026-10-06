#include <gtest/gtest.h>

#include "engine/assets/resources_manager.hpp"

using namespace Shard::Engine;
using Core::Resources::AssetKind;
using Core::Resources::AssetKindInfo;
using Core::Resources::ResourceKey;

namespace {

    struct Tex {
        Filesystem::AssetID id;
    };
    struct Mat {};
    struct Shd {
        Filesystem::AssetID id;
    };
    struct Lvl {};

    // A resources manager over an in-memory asset database and made-up resource types, standing for
    // what the renderer / world register for real.
    class ResourcesManagerTest : public ::testing::Test {
    protected:
        Filesystem::AssetIDManager ids;
        Filesystem::FileManager files{ids};
        Core::Resources::ResourcesManager rm{files, ids};

        int loads = 0;
        int adoptedIDs = 0;
        std::vector<ResourceKey> materialDeps;
        std::vector<ResourceKey> worldDeps;

        void AddAsset(int id, const std::string& nameInProject, Filesystem::Type type = Filesystem::Type::T_IMAGE)
        {
            auto info = std::make_shared<Filesystem::AssetInfos>();
            info->baseInfos.nameInProject = nameInProject;
            info->baseInfos.name = nameInProject;
            info->baseInfos.type = type;
            ids.AssignID(ids.GenerateNewIDWithValue(id), info);
        }

        std::shared_ptr<Filesystem::AssetInfos> Info(const std::string& nameInProject)
        {
            return ids.GetAssetFromID(ids.GetIDFromNameInProject(nameInProject));
        }

        void SetUp() override
        {
            {
                AssetKindInfo info;
                info.load = [this](const std::string&, const Filesystem::AssetInfos&) -> std::shared_ptr<void> {
                    loads++;
                    return std::make_shared<Tex>();
                };
                info.setAssetID = [this](void* r, Filesystem::AssetID id) { static_cast<Tex*>(r)->id = id; adoptedIDs++; };
                rm.RegisterKind(AssetKind::Texture, std::move(info));
            }
            {
                AssetKindInfo info; // keyed without ".vert", database entry is "<path>.vert"
                info.load = [this](const std::string&, const Filesystem::AssetInfos&) -> std::shared_ptr<void> {
                    loads++;
                    return std::make_shared<Shd>();
                };
                info.setAssetID = [](void* r, Filesystem::AssetID id) { static_cast<Shd*>(r)->id = id; };
                info.nameSuffix = ".vert";
                info.dependencySuffixes = {".vert", ".frag"};
                rm.RegisterKind(AssetKind::Shader, std::move(info));
            }
            {
                AssetKindInfo info; // keeps its textures and shader alive
                info.load = [this](const std::string&, const Filesystem::AssetInfos&) -> std::shared_ptr<void> {
                    loads++;
                    return std::make_shared<Mat>();
                };
                info.ownerType = Filesystem::Type::T_MATERIAL;
                info.dependencies = [this](const Filesystem::Path&) { return materialDeps; };
                rm.RegisterKind(AssetKind::Material, std::move(info));
            }
            {
                AssetKindInfo info; // owned by the world manager in the engine: never swept
                info.load = [this](const std::string&, const Filesystem::AssetInfos&) -> std::shared_ptr<void> {
                    loads++;
                    return std::make_shared<Lvl>();
                };
                info.ownerType = Filesystem::Type::T_WORLD;
                info.evictable = false;
                info.dependencies = [this](const Filesystem::Path&) { return worldDeps; };
                rm.RegisterKind(AssetKind::World, std::move(info));
            }
        }
    };

    // ---- cache -------------------------------------------------------------------------------

    TEST_F(ResourcesManagerTest, GetLoadsOnceThenServesFromTheCache)
    {
        AddAsset(501, "textures/a.png");

        auto first = rm.Get<Tex>(AssetKind::Texture, "textures/a.png");
        auto second = rm.Get<Tex>(AssetKind::Texture, "textures/a.png");

        ASSERT_NE(first, nullptr);
        EXPECT_EQ(first, second);
        EXPECT_EQ(loads, 1);
    }

    TEST_F(ResourcesManagerTest, UnknownAssetReturnsNullWithoutCallingTheLoader)
    {
        EXPECT_EQ(rm.Get<Tex>(AssetKind::Texture, "textures/missing.png"), nullptr);
        EXPECT_EQ(loads, 0);
        EXPECT_FALSE(rm.Has(AssetKind::Texture, "textures/missing.png"));
    }

    TEST_F(ResourcesManagerTest, KindWithoutLoaderReturnsNull)
    {
        AddAsset(501, "textures/a.png");
        EXPECT_EQ(rm.Get<Tex>(AssetKind::FirstUser, "textures/a.png"), nullptr);
    }

    TEST_F(ResourcesManagerTest, LoadedResourceGetsItsAssetID)
    {
        AddAsset(501, "textures/a.png");

        auto tex = rm.Get<Tex>(AssetKind::Texture, "textures/a.png");

        ASSERT_NE(tex, nullptr);
        EXPECT_EQ(tex->id.GetAsInt(), 501);
    }

    TEST_F(ResourcesManagerTest, ShaderIsKeyedWithoutExtensionButFoundByItsVertFile)
    {
        AddAsset(502, "shaders/s.vert", Filesystem::Type::T_SHADER);

        auto shader = rm.Get<Shd>(AssetKind::Shader, "shaders/s");

        ASSERT_NE(shader, nullptr);
        EXPECT_EQ(shader->id.GetAsInt(), 502);
        EXPECT_TRUE(rm.Has(AssetKind::Shader, "shaders/s"));
        EXPECT_EQ(rm.Get<Shd>(AssetKind::Shader, "shaders/other"), nullptr);
    }

    TEST_F(ResourcesManagerTest, AdoptInsertsOnceAndKeepsTheFirstOne)
    {
        AddAsset(501, "textures/a.png");

        auto mine = std::make_shared<Tex>();
        rm.Adopt(AssetKind::Texture, "textures/a.png", mine);
        EXPECT_TRUE(rm.Has(AssetKind::Texture, "textures/a.png"));
        EXPECT_EQ(mine->id.GetAsInt(), 501);
        EXPECT_EQ(adoptedIDs, 1);

        rm.Adopt(AssetKind::Texture, "textures/a.png", std::make_shared<Tex>());

        EXPECT_EQ(rm.Get<Tex>(AssetKind::Texture, "textures/a.png"), mine);
        EXPECT_EQ(adoptedIDs, 1);
        EXPECT_EQ(loads, 0);
    }

    TEST_F(ResourcesManagerTest, UnloadDropsFromTheCacheAndTheNextGetReloads)
    {
        AddAsset(501, "textures/a.png");
        rm.Get<Tex>(AssetKind::Texture, "textures/a.png");

        rm.Unload(AssetKind::Texture, "textures/a.png");
        EXPECT_FALSE(rm.Has(AssetKind::Texture, "textures/a.png"));

        rm.Get<Tex>(AssetKind::Texture, "textures/a.png");
        EXPECT_EQ(loads, 2);
    }

    // ---- dependency graph and eviction -------------------------------------------------------

    TEST_F(ResourcesManagerTest, OwnerRecordsItsDependenciesInTheAssetDatabase)
    {
        AddAsset(501, "textures/a.png");
        AddAsset(502, "shaders/s.vert", Filesystem::Type::T_SHADER);
        AddAsset(503, "shaders/s.frag", Filesystem::Type::T_SHADER);
        AddAsset(504, "materials/m.mat", Filesystem::Type::T_MATERIAL);
        materialDeps = {{AssetKind::Shader, "shaders/s"}, {AssetKind::Texture, "textures/a.png"}};

        rm.Get<Mat>(AssetKind::Material, "materials/m.mat");

        // a shader stands for its .vert and .frag files, in the order of the suffixes
        std::vector<int> recorded;
        for(auto& dep : Info("materials/m.mat")->dependencies)
            recorded.push_back(dep.GetAsInt());
        EXPECT_EQ(recorded, (std::vector<int>{502, 503, 501}));
    }

    TEST_F(ResourcesManagerTest, DependencyIsEvictedWhenItsOwnerIsUnloaded)
    {
        AddAsset(501, "textures/a.png");
        AddAsset(502, "textures/b.png");
        AddAsset(503, "materials/m.mat", Filesystem::Type::T_MATERIAL);
        materialDeps = {{AssetKind::Texture, "textures/a.png"}, {AssetKind::Texture, "textures/b.png"}};

        rm.Get<Tex>(AssetKind::Texture, "textures/a.png");
        rm.Get<Tex>(AssetKind::Texture, "textures/b.png");
        rm.Get<Mat>(AssetKind::Material, "materials/m.mat");

        EXPECT_EQ(rm.CollectUnused(), 0); // retained by the material

        rm.Unload(AssetKind::Material, "materials/m.mat");

        EXPECT_EQ(rm.CollectUnused(), 2);
        EXPECT_FALSE(rm.Has(AssetKind::Texture, "textures/a.png"));
        EXPECT_FALSE(rm.Has(AssetKind::Texture, "textures/b.png"));
    }

    TEST_F(ResourcesManagerTest, SharedDependencySurvivesUntilItsLastOwnerIsUnloaded)
    {
        AddAsset(501, "textures/a.png");
        AddAsset(502, "materials/m1.mat", Filesystem::Type::T_MATERIAL);
        AddAsset(503, "materials/m2.mat", Filesystem::Type::T_MATERIAL);
        materialDeps = {{AssetKind::Texture, "textures/a.png"}};

        rm.Get<Tex>(AssetKind::Texture, "textures/a.png");
        rm.Get<Mat>(AssetKind::Material, "materials/m1.mat");
        rm.Get<Mat>(AssetKind::Material, "materials/m2.mat");

        rm.Unload(AssetKind::Material, "materials/m1.mat");
        EXPECT_EQ(rm.CollectUnused(), 0);
        EXPECT_TRUE(rm.Has(AssetKind::Texture, "textures/a.png"));

        rm.Unload(AssetKind::Material, "materials/m2.mat");
        EXPECT_EQ(rm.CollectUnused(), 1);
        EXPECT_FALSE(rm.Has(AssetKind::Texture, "textures/a.png"));
    }

    TEST_F(ResourcesManagerTest, ResourceHeldOutsideTheCacheIsKeptAndSweptLater)
    {
        AddAsset(501, "textures/a.png");
        AddAsset(502, "materials/m.mat", Filesystem::Type::T_MATERIAL);
        materialDeps = {{AssetKind::Texture, "textures/a.png"}};

        auto held = rm.Get<Tex>(AssetKind::Texture, "textures/a.png"); // e.g. a Model holding it
        rm.Get<Mat>(AssetKind::Material, "materials/m.mat");
        rm.Unload(AssetKind::Material, "materials/m.mat");

        EXPECT_EQ(rm.CollectUnused(), 0);
        EXPECT_TRUE(rm.Has(AssetKind::Texture, "textures/a.png"));

        held.reset();
        EXPECT_EQ(rm.CollectUnused(), 1); // still a candidate: it was deferred, not forgotten
    }

    TEST_F(ResourcesManagerTest, EngineResourcesAreNeverEvicted)
    {
        AddAsset(100, "textures/white.png"); // engine resource: id <= 500
        AddAsset(501, "materials/m.mat", Filesystem::Type::T_MATERIAL);
        materialDeps = {{AssetKind::Texture, "textures/white.png"}};

        rm.Get<Tex>(AssetKind::Texture, "textures/white.png");
        rm.Get<Mat>(AssetKind::Material, "materials/m.mat");
        rm.Unload(AssetKind::Material, "materials/m.mat");

        EXPECT_EQ(rm.CollectUnused(), 0);
        EXPECT_TRUE(rm.Has(AssetKind::Texture, "textures/white.png"));
    }

    TEST_F(ResourcesManagerTest, WorldIsNeverSweptButReleasesItsDependencies)
    {
        AddAsset(501, "textures/a.png");
        AddAsset(502, "worlds/l.world", Filesystem::Type::T_WORLD);
        worldDeps = {{AssetKind::Texture, "textures/a.png"}};

        rm.Get<Tex>(AssetKind::Texture, "textures/a.png");
        auto world = rm.Get<Lvl>(AssetKind::World, "worlds/l.world");
        ASSERT_NE(world, nullptr);

        rm.UnLoadDependencies("worlds/l.world"); // unloads the world (owned elsewhere) then sweeps

        EXPECT_FALSE(rm.Has(AssetKind::World, "worlds/l.world"));
        EXPECT_FALSE(rm.Has(AssetKind::Texture, "textures/a.png"));
    }

    TEST_F(ResourcesManagerTest, UnLoadDependenciesLeavesAMaterialThatIsStillNeeded)
    {
        AddAsset(501, "materials/m.mat", Filesystem::Type::T_MATERIAL);

        auto held = rm.Get<Mat>(AssetKind::Material, "materials/m.mat"); // e.g. a Model holding it

        rm.UnLoadDependencies("materials/m.mat");
        EXPECT_TRUE(rm.Has(AssetKind::Material, "materials/m.mat"));

        held.reset();
        rm.UnLoadDependencies("materials/m.mat");
        EXPECT_FALSE(rm.Has(AssetKind::Material, "materials/m.mat"));
    }

    TEST_F(ResourcesManagerTest, RefreshDependenciesUpdatesAnOwnerThatIsNotResident)
    {
        AddAsset(501, "textures/a.png");
        AddAsset(502, "textures/b.png");
        AddAsset(503, "materials/m.mat", Filesystem::Type::T_MATERIAL);

        materialDeps = {{AssetKind::Texture, "textures/a.png"}};
        rm.RefreshDependencies("materials/m.mat");
        ASSERT_EQ(Info("materials/m.mat")->dependencies.size(), 1u);
        EXPECT_EQ(Info("materials/m.mat")->dependencies[0].GetAsInt(), 501);

        materialDeps = {{AssetKind::Texture, "textures/b.png"}, {AssetKind::Texture, "textures/b.png"}};
        rm.RefreshDependencies("materials/m.mat");
        ASSERT_EQ(Info("materials/m.mat")->dependencies.size(), 1u); // duplicates folded
        EXPECT_EQ(Info("materials/m.mat")->dependencies[0].GetAsInt(), 502);
        EXPECT_FALSE(rm.Has(AssetKind::Material, "materials/m.mat")); // it was never loaded
    }

    // ---- in use / unloading an asset ---------------------------------------------------------

    TEST_F(ResourcesManagerTest, AssetHeldOutsideTheCacheIsInUseAndCannotBeUnloaded)
    {
        AddAsset(501, "textures/a.png");

        auto held = rm.Get<Tex>(AssetKind::Texture, "textures/a.png");

        EXPECT_TRUE(rm.IsInUse("textures/a.png"));
        EXPECT_FALSE(rm.TryUnloadAsset("textures/a.png"));
        EXPECT_TRUE(rm.Has(AssetKind::Texture, "textures/a.png"));

        held.reset();
        EXPECT_FALSE(rm.IsInUse("textures/a.png"));
        EXPECT_TRUE(rm.TryUnloadAsset("textures/a.png"));
        EXPECT_FALSE(rm.Has(AssetKind::Texture, "textures/a.png"));
    }

    TEST_F(ResourcesManagerTest, AssetRetainedByAnOwnerIsInUse)
    {
        AddAsset(501, "textures/a.png");
        AddAsset(502, "materials/m.mat", Filesystem::Type::T_MATERIAL);
        materialDeps = {{AssetKind::Texture, "textures/a.png"}};

        rm.Get<Mat>(AssetKind::Material, "materials/m.mat");

        EXPECT_TRUE(rm.IsInUse("textures/a.png"));
        EXPECT_FALSE(rm.TryUnloadAsset("textures/a.png"));
    }

    TEST_F(ResourcesManagerTest, ShaderUseIsFoundFromAnyOfItsFiles)
    {
        AddAsset(501, "shaders/s.vert", Filesystem::Type::T_SHADER);
        AddAsset(502, "materials/m.mat", Filesystem::Type::T_MATERIAL);
        materialDeps = {{AssetKind::Shader, "shaders/s"}};

        rm.Get<Mat>(AssetKind::Material, "materials/m.mat");

        EXPECT_TRUE(rm.IsInUse("shaders/s.vert"));
        EXPECT_TRUE(rm.IsInUse("shaders/s.frag"));
        EXPECT_FALSE(rm.IsInUse("shaders/other.vert"));
    }

}

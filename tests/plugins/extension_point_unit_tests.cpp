#include <gtest/gtest.h>

#include "engine/assets/asset_importer.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/renderer/frontend/pipelines/render_pipeline.hpp"
#include "plugins/plugin_test_utils.hpp"

using namespace Shard::Engine;
using Core::Resources::AssetKind;
using Core::Resources::AssetKindInfo;
using PluginTest::TempDir;

TEST(UserAssetKinds, AreAllocatedAboveTheEnginesAndLeaveWithTheirPlugin)
{
    Filesystem::AssetIDManager ids;
    Filesystem::FileManager files{ ids };
    Core::Resources::ResourcesManager rm{ files, ids };

    int loads = 0;
    auto makeInfo = [&] {
        AssetKindInfo info;
        info.load = [&](const std::string&, const Filesystem::AssetInfos&) -> std::shared_ptr<void> { ++loads; return std::make_shared<int>(7); };
        return info;
    };

    const AssetKind first = rm.AllocateUserKind();
    EXPECT_GE(static_cast<int>(first), static_cast<int>(AssetKind::FirstUser));
    rm.RegisterKind(first, makeInfo());
    const AssetKind second = rm.AllocateUserKind();
    EXPECT_NE(first, second);
    rm.RegisterKind(second, makeInfo());

    // a file of the first kind, loaded and cached
    auto asset = std::make_shared<Filesystem::AssetInfos>();
    asset->baseInfos.nameInProject = "thing.foo";
    asset->baseInfos.name = "thing.foo";
    ids.AssignID(ids.GenerateNewIDWithValue(5), asset);
    auto a = rm.Get<int>(first, "thing.foo");
    ASSERT_TRUE(a);
    EXPECT_EQ(*a, 7);
    EXPECT_TRUE(rm.Has(first, "thing.foo"));

    EXPECT_TRUE(rm.UnregisterKind(first));
    EXPECT_FALSE(rm.Has(first, "thing.foo"));          // what was cached is dropped
    EXPECT_EQ(a.use_count(), 1);                          // only the test holds it now
    EXPECT_FALSE(rm.Get<int>(first, "thing.foo"));        // no loader any more
    EXPECT_FALSE(rm.UnregisterKind(first));
    EXPECT_EQ(rm.AllocateUserKind(), first);              // the id is free again
    EXPECT_TRUE(rm.Get<int>(second, "thing.foo"));
}

TEST(CustomExtensions, MakeFilesAssetsOfTheCustomType)
{
    EXPECT_EQ(Filesystem::Path("a/b/c.zzcustom").GetExtensionType(), Filesystem::Type::T_TEXT);
    EXPECT_TRUE(Filesystem::RegisterCustomExtension(".zzcustom"));
    EXPECT_EQ(Filesystem::Path("a/b/c.zzcustom").GetExtensionType(), Filesystem::Type::T_CUSTOM);
    EXPECT_EQ(Filesystem::Path("a/b/c.ZZCUSTOM").GetExtensionType(), Filesystem::Type::T_CUSTOM);          // case does not matter
    EXPECT_TRUE(Filesystem::IsCustomExtension("zzcustom"));
    EXPECT_TRUE(Filesystem::RegisterCustomExtension("zzcustom"));                                                // again : fine
    EXPECT_TRUE(Filesystem::UnregisterCustomExtension(".zzcustom"));
    EXPECT_EQ(Filesystem::Path("a/b/c.zzcustom").GetExtensionType(), Filesystem::Type::T_TEXT);
    EXPECT_FALSE(Filesystem::UnregisterCustomExtension(".zzcustom"));
}

TEST(CustomExtensions, TheEnginesOwnExtensionsCannotBeTaken)
{
    for (const char* ext : { ".png", ".world", ".mat", ".fbx", ".json", ".txt", ".md", ".wav", "", "." })
        EXPECT_FALSE(Filesystem::RegisterCustomExtension(ext)) << ext;
    EXPECT_EQ(Filesystem::Path("x.png").GetExtensionType(), Filesystem::Type::T_IMAGE);
}

TEST(AssetImporters, FindByExtensionAndRunWithErrorsContained)
{
    TempDir dir;
    dir.Write("in/model.foobar", "contents");
    dir.Write("in/other.unknownext", "x");

    auto importer = std::make_shared<Filesystem::AssetImporterExtension>();
    importer->name = "foobar";
    importer->extensions = { ".foobar", "FOOBAR2" };
    importer->import = [](const std::string& source, const std::string& out) {
        Filesystem::ImportResult r;
        if (source.find("explode") != std::string::npos) throw std::runtime_error("boom");
        std::ofstream f(out + "/model.mesh");
        f << "converted";
        r.ok = true;
        r.outputs = { out + "/model.mesh" };
        return r;
    };
    auto& importers = Filesystem::AssetImporters::Get();
    importers.Register(importer);

    EXPECT_TRUE(importers.CanImport(dir.Sub("in/model.foobar")));
    EXPECT_TRUE(importers.CanImport("x/y.foobar2"));
    EXPECT_TRUE(importers.CanImport("x/y.FOOBAR"));
    EXPECT_FALSE(importers.CanImport(dir.Sub("in/other.unknownext")));
    EXPECT_FALSE(importers.CanImport("noextension"));

    auto done = importers.Import(dir.Sub("in/model.foobar"), dir.Sub("out"));
    ASSERT_TRUE(done.ok) << done.message;
    ASSERT_EQ(done.outputs.size(), 1u);
    EXPECT_EQ(dir.Read("out/model.mesh"), "converted");

    EXPECT_FALSE(importers.Import(dir.Sub("in/missing.foobar"), dir.Sub("out")).ok);
    EXPECT_FALSE(importers.Import(dir.Sub("in/other.unknownext"), dir.Sub("out")).ok);
    dir.Write("in/explode.foobar", "x");
    auto exploded = importers.Import(dir.Sub("in/explode.foobar"), dir.Sub("out"));
    EXPECT_FALSE(exploded.ok);
    EXPECT_NE(exploded.message.find("boom"), std::string::npos);

    EXPECT_FALSE(importers.Extensions().empty());
    importers.Unregister(importer);
    EXPECT_FALSE(importers.CanImport(dir.Sub("in/model.foobar")));
}

namespace {
    class NullPipeline final : public Rendering::RenderPipeline {
    public:
        const char* GetType() const override { return "zz-unit-pipeline"; }
        void Activate(Rendering::Renderer&) override {}
        void Deactivate(Rendering::Renderer&) override {}
    };
}

TEST(RenderPipelineRegistry, TypesComeAndGo)
{
    auto& registry = Rendering::RenderPipelineRegistry::Get();
    EXPECT_FALSE(registry.Has("zz-unit-pipeline"));
    registry.RegisterType("zz-unit-pipeline", "a test", [](const nlohmann::json&) { return std::make_unique<NullPipeline>(); });
    EXPECT_TRUE(registry.Has("zz-unit-pipeline"));
    auto made = registry.Create("zz-unit-pipeline");
    ASSERT_NE(made, nullptr);
    EXPECT_STREQ(made->GetType(), "zz-unit-pipeline");
    bool listed = false;
    for (const auto& info : registry.List()) if (info.name == "zz-unit-pipeline") listed = true;
    EXPECT_TRUE(listed);

    EXPECT_TRUE(registry.UnregisterType("zz-unit-pipeline"));
    EXPECT_FALSE(registry.UnregisterType("zz-unit-pipeline"));
    EXPECT_FALSE(registry.Has("zz-unit-pipeline"));
    EXPECT_EQ(registry.Create("zz-unit-pipeline"), nullptr);
}

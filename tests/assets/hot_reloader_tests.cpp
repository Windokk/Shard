#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "engine/assets/hot_reloader.hpp"
#include "engine/assets/resources_manager.hpp"

using namespace Shard::Engine;
namespace fs = std::filesystem;
using Core::Resources::AssetKind;
using Core::Resources::AssetKindInfo;

namespace {

    struct Tex {
        int version = 0;
    };

    // The hot reloader over a real folder, with a made-up texture kind that counts its reloads
    class HotReloaderTest : public ::testing::Test {
    protected:
        fs::path root;
        Filesystem::AssetIDManager ids;
        Filesystem::FileManager files{ids};
        Core::Resources::ResourcesManager rm{files, ids};
        Assets::HotReloader reloader;
        int reloads = 0;
        int databaseSaves = 0;

        void SetUp() override
        {
            root = fs::temp_directory_path() / ("shard_hot_reload_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(root / "resources" / "textures");
            fs::create_directories(root / "engine");

            const std::string resources = fs::absolute(root / "resources").generic_string();
            files.Init(Filesystem::Path(resources, true), Filesystem::Path(fs::absolute(root / "engine").generic_string(), true),
                       Filesystem::Path(fs::absolute(root).generic_string(), true));
            ids.InitCounter(501);

            AssetKindInfo info;
            info.load = [](const std::string&, const Filesystem::AssetInfos&) -> std::shared_ptr<void> { return std::make_shared<Tex>(); };
            info.reload = [this](void* existing, const std::string&, const Filesystem::AssetInfos&) {
                static_cast<Tex*>(existing)->version++;
                reloads++;
                return true;
            };
            rm.RegisterKind(AssetKind::Texture, std::move(info));
        }

        void TearDown() override
        {
            reloader.Stop();
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        void Write(const std::string& name, const std::string& content)
        {
            fs::create_directories((root / "resources" / name).parent_path());
            std::ofstream(root / "resources" / name, std::ios::binary | std::ios::trunc) << content;
        }

        bool StartReloader()
        {
            Assets::HotReloader::Config config;
            config.files = &files;
            config.ids = &ids;
            config.resources = &rm;
            config.persistDatabase = [this] { databaseSaves++; };
            reloader.SetSettleTime(0.05);
            return reloader.Start(config);
        }

        template <class Predicate>
        bool Pump(Predicate done)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline) {
                reloader.Update();
                if (done())
                    return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            return false;
        }

        bool Registered(const std::string& name)
        {
            auto info = ids.GetAssetFromID(ids.GetIDFromNameInProject(name));
            return info && info->baseInfos.nameInProject == name;
        }
    };

    TEST_F(HotReloaderTest, ANewFileGetsRegistered)
    {
        ASSERT_TRUE(StartReloader());
        const uint64_t revision = reloader.GetRevision();

        Write("textures/new.png", "pixels");

        ASSERT_TRUE(Pump([&] { return Registered("textures/new.png"); }));
        EXPECT_GT(reloader.GetRevision(), revision);
        EXPECT_GE(databaseSaves, 1);
    }

    TEST_F(HotReloaderTest, AModifiedResidentResourceIsReloadedInPlace)
    {
        Write("textures/a.png", "one");
        files.RegisterAsset(Filesystem::Path((root / "resources" / "textures" / "a.png").generic_string(), true));
        auto texture = rm.Get<Tex>(AssetKind::Texture, "textures/a.png");
        ASSERT_NE(texture, nullptr);

        ASSERT_TRUE(StartReloader());
        Write("textures/a.png", "two");

        ASSERT_TRUE(Pump([&] { return reloads > 0; }));
        EXPECT_EQ(texture->version, 1);
        EXPECT_EQ(rm.Get<Tex>(AssetKind::Texture, "textures/a.png"), texture);
    }

    TEST_F(HotReloaderTest, ARenamedFileKeepsItsIDAndItsResidentResource)
    {
        Write("textures/a.png", "one");
        const Filesystem::AssetID id = files.RegisterAsset(Filesystem::Path((root / "resources" / "textures" / "a.png").generic_string(), true));
        { auto texture = rm.Get<Tex>(AssetKind::Texture, "textures/a.png"); ASSERT_NE(texture, nullptr); }

        ASSERT_TRUE(StartReloader());
        fs::rename(root / "resources" / "textures" / "a.png", root / "resources" / "textures" / "b.png");

        ASSERT_TRUE(Pump([&] { return Registered("textures/b.png"); }));
        EXPECT_FALSE(Registered("textures/a.png"));
        EXPECT_EQ(ids.GetIDFromNameInProject("textures/b.png"), id);
        EXPECT_TRUE(rm.Has(AssetKind::Texture, "textures/b.png"));
        EXPECT_FALSE(rm.Has(AssetKind::Texture, "textures/a.png"));
    }

    TEST_F(HotReloaderTest, ARenamedFolderMovesItsAssets)
    {
        Write("textures/sub/a.png", "one");
        const Filesystem::AssetID id = files.RegisterAsset(Filesystem::Path((root / "resources" / "textures" / "sub" / "a.png").generic_string(), true));

        ASSERT_TRUE(StartReloader());
        fs::rename(root / "resources" / "textures" / "sub", root / "resources" / "textures" / "moved");

        ASSERT_TRUE(Pump([&] { return Registered("textures/moved/a.png"); }));
        EXPECT_EQ(ids.GetIDFromNameInProject("textures/moved/a.png"), id);
        EXPECT_FALSE(Registered("textures/sub/a.png"));
    }

    TEST_F(HotReloaderTest, ADeletedFileIsUnregisteredAndUnloaded)
    {
        Write("textures/a.png", "one");
        files.RegisterAsset(Filesystem::Path((root / "resources" / "textures" / "a.png").generic_string(), true));
        { auto texture = rm.Get<Tex>(AssetKind::Texture, "textures/a.png"); ASSERT_NE(texture, nullptr); }

        ASSERT_TRUE(StartReloader());
        fs::remove(root / "resources" / "textures" / "a.png");

        ASSERT_TRUE(Pump([&] { return !Registered("textures/a.png"); }));
        EXPECT_FALSE(rm.Has(AssetKind::Texture, "textures/a.png"));
    }

    TEST_F(HotReloaderTest, ADeletedFileStillUsedStaysRegistered)
    {
        Write("textures/a.png", "one");
        files.RegisterAsset(Filesystem::Path((root / "resources" / "textures" / "a.png").generic_string(), true));
        auto held = rm.Get<Tex>(AssetKind::Texture, "textures/a.png");
        ASSERT_NE(held, nullptr);

        ASSERT_TRUE(StartReloader());
        fs::remove(root / "resources" / "textures" / "a.png");

        // Give the change time to be seen and applied : the asset must still be there afterwards
        Pump([] { return false; });
        EXPECT_TRUE(Registered("textures/a.png"));
        EXPECT_TRUE(rm.Has(AssetKind::Texture, "textures/a.png"));
    }

    TEST_F(HotReloaderTest, AFileWrittenUnderATemporaryNameThenRenamedOntoTheRealOneIsAModification)
    {
        Write("textures/a.png", "one");
        files.RegisterAsset(Filesystem::Path((root / "resources" / "textures" / "a.png").generic_string(), true));
        auto texture = rm.Get<Tex>(AssetKind::Texture, "textures/a.png");
        ASSERT_NE(texture, nullptr);

        ASSERT_TRUE(StartReloader());
        Write("textures/a.png.tmp", "two");
        fs::rename(root / "resources" / "textures" / "a.png.tmp", root / "resources" / "textures" / "a.png");

        ASSERT_TRUE(Pump([&] { return reloads > 0; }));
        EXPECT_TRUE(Registered("textures/a.png"));
        EXPECT_FALSE(Registered("textures/a.png.tmp"));
    }
}

#pragma once

#include "engine/assets/assetID.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/assets/vfs/filesystem.hpp"
#include "support/test_engine_context.hpp"

namespace PluginTest {

    /// A test engine whose asset database and file manager exist (and are empty): the fields that hold an asset are written by name through the
    /// first, and a world that is told about plugins asks the second for its asset id
    class AssetContext : public Shard::Tests::TestEngineContext {
    public:
        AssetContext()
        {
            using Shard::Engine::Filesystem::Path;
            m_Files.Init(Path("project/resources"), Path("engine/resources"), Path("project"));
        }
        Shard::Engine::Filesystem::AssetIDManager* GetAssetIDManager() const override
        {
            return const_cast<Shard::Engine::Filesystem::AssetIDManager*>(&m_Assets);
        }
        Shard::Engine::Filesystem::FileManager* GetFileManager() const override
        {
            return const_cast<Shard::Engine::Filesystem::FileManager*>(&m_Files);
        }
        Shard::Engine::Core::Resources::ResourcesManager* GetResourcesManager() const override
        {
            return const_cast<Shard::Engine::Core::Resources::ResourcesManager*>(&m_Resources);
        }
    private:
        Shard::Engine::Filesystem::AssetIDManager m_Assets;
        Shard::Engine::Filesystem::FileManager m_Files{ m_Assets };
        Shard::Engine::Core::Resources::ResourcesManager m_Resources{ m_Files, m_Assets };
    };
}

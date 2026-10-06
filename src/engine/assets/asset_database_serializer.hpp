#pragma once

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Serialization{

    void DeserializeAssetDataBase(const Filesystem::Path resourcesPath, const Filesystem::Path databasePath,
                                  Filesystem::FileManager& files, Filesystem::AssetIDManager& ids);
    void SerializeAssetDataBase(const Filesystem::Path databasePath, const Filesystem::AssetIDManager& ids);
}
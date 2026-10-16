#pragma once

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Serialization{

    /// @param firstID The lowest ID this database may hold (0 for the engine's, kFirstProjectAssetID for a project's) : an
    /// entry below it, or on an ID already taken (an engine resource), is given a new ID instead of overwriting the other one.
    void DeserializeAssetDataBase(const Filesystem::Path resourcesPath, const Filesystem::Path databasePath,
                                  Filesystem::FileManager& files, Filesystem::AssetIDManager& ids, int firstID = 0);
    void SerializeAssetDataBase(const Filesystem::Path databasePath, const Filesystem::AssetIDManager& ids);
}

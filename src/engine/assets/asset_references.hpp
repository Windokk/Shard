#pragma once

#include <string>

#include "engine/assets/assetID.hpp"
#include "engine/assets/project/project.hpp"

// What has to follow when an asset is renamed, moved or deleted : the files that name it by its path in the
// project, and the build settings. Shared by the editor's asset operations and the hot reload (a rename done
// outside the editor).
namespace Shard::Engine::Assets
{
    /// @brief True if `name` is inside the folder `dirName` (both paths in the project).
    bool IsUnderFolder(const std::string& name, const std::string& dirName);

    /// @brief Worlds and materials name the assets they use by their path in the project : rewrites those strings
    /// after a rename/move of `oldName` (a folder if `isDirectory`). Returns the number of files rewritten.
    int RewriteAssetReferences(Filesystem::AssetIDManager& ids, const std::string& oldName, const std::string& newName, bool isDirectory);

    /// @brief Follows a rename/move (or, with an empty `newName`, a deletion) in the project's list of worlds.
    void RemapBuildSettings(Projects::BuildSettings& settings, const Filesystem::Path& projectResources,
                            const std::string& oldName, const std::string& newName, bool isDirectory);
}

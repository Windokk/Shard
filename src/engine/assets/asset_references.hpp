#pragma once

#include <string>

#include "engine/assets/assetID.hpp"
#include "engine/assets/project/project.hpp"

namespace Shard::Engine::Assets
{
    bool IsUnderFolder(const std::string& name, const std::string& dirName);

    int RewriteAssetReferences(Filesystem::AssetIDManager& ids, const std::string& oldName, const std::string& newName, bool isDirectory);

    void RemapBuildSettings(Projects::BuildSettings& settings, const Filesystem::Path& projectResources,
                            const std::string& oldName, const std::string& newName, bool isDirectory);
}

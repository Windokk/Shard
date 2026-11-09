#pragma once

#include <string>

#include "engine/assets/assetID.hpp"
#include "engine/plugins/content.hpp"

namespace Shard::Engine::Plugins {

    // Puts the files of a mounted module in the asset database, under their mod://<id>/<path> name. Returns how many.
    size_t RegisterModuleAssets(const ContentMounts& content, const std::string& pluginId, Filesystem::AssetIDManager& ids);
    size_t UnregisterModuleAssets(const std::string& pluginId, Filesystem::AssetIDManager& ids);

    // The same on the asset database of the running engine (nothing when there is none)
    size_t RegisterModuleAssets(const ContentMounts& content, const std::string& pluginId);
    size_t UnregisterModuleAssets(const std::string& pluginId);
}

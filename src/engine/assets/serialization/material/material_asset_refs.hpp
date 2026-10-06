#pragma once

#include <string>
#include <vector>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Serialization{

    // Asset paths (pathInProject) referenced by a material file, without loading/compiling/uploading
    // anything - pure JSON parsing, no GL or engine-singleton calls. Used by the async world loader's
    // manifest-building pass to know what to prefetch, and by the resources manager to know what a
    // material keeps alive.
    struct MaterialAssetRefs
    {
        bool success = false;
        std::string shaderPathInProject;
        std::vector<std::string> texturePathsInProject;
    };

    MaterialAssetRefs PeekMaterialAssetRefs(const Filesystem::Path& path);

}

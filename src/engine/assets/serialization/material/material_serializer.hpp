#pragma once

#include <string>
#include <vector>

#include "engine/filesystem/filesystem.hpp"

namespace Shard::Engine::Rendering{
    class Material;
}

namespace Shard::Engine::Serialization{

    std::shared_ptr<Rendering::Material> DeserializeMaterial(const Filesystem::Path path);

    // Asset paths (pathInProject) referenced by a material file, without loading/compiling/uploading
    // anything - pure JSON parsing, no GL or engine-singleton calls. Used by the async level loader's
    // manifest-building pass to know what to prefetch.
    struct MaterialAssetRefs
    {
        bool success = false;
        std::string shaderPathInProject;
        std::vector<std::string> texturePathsInProject;
    };

    MaterialAssetRefs PeekMaterialAssetRefs(const Filesystem::Path& path);

}
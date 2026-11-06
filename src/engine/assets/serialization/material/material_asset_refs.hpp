#pragma once

#include <string>
#include <vector>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Serialization{

    // Colour slots (albedo, diffuse, emission) hold sRGB images and load as ColorTexture. Every other slot is plain data.
    bool IsColorTextureSlot(const std::string& samplerName);

    struct MaterialTextureRef
    {
        std::string slot;
        std::string pathInProject;
    };

    struct MaterialAssetRefs
    {
        bool success = false;
        std::string shaderPathInProject;
        std::vector<std::string> texturePathsInProject;
        std::vector<MaterialTextureRef> textures;
    };

    MaterialAssetRefs PeekMaterialAssetRefs(const Filesystem::Path& path);

}

#pragma once

#include <string>
#include <vector>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Serialization{

    // Asset paths (pathInProject) referenced by a material file, without loading/compiling/uploading
    // anything - pure JSON parsing, no GL or engine-singleton calls. Used by the async world loader's
    // manifest-building pass to know what to prefetch, and by the resources manager to know what a
    // material keeps alive.
    /// Whether a texture slot of a material (the name of the sampler in its shader) holds colours : an albedo, a diffuse
    /// colour or an emission map. Those images are painted in the sRGB transfer curve and have to be read as such (the
    /// ColorTexture kind) ; every other slot (normals, metalness, roughness, masks) holds plain data.
    bool IsColorTextureSlot(const std::string& samplerName);

    struct MaterialTextureRef
    {
        std::string slot;               // the sampler the texture is bound to
        std::string pathInProject;
    };

    struct MaterialAssetRefs
    {
        bool success = false;
        std::string shaderPathInProject;
        std::vector<std::string> texturePathsInProject;
        std::vector<MaterialTextureRef> textures;       // the same textures, with the slot each is for
    };

    MaterialAssetRefs PeekMaterialAssetRefs(const Filesystem::Path& path);

}

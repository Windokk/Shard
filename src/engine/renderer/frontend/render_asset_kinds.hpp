#pragma once

namespace Shard::Engine::Core::Resources{
    class ResourcesManager;
}

namespace Shard::Engine::Rendering{

    /// Registers the kinds of resources the renderer owns with the resources manager : meshes, textures,
    /// environment maps, shaders, compute shaders, materials and baked probe volumes. Must run before
    /// any of them is requested.
    void RegisterAssetKinds(Core::Resources::ResourcesManager& resources);

}

#pragma once

namespace Shard::Engine::Rendering {

    /// @brief The anisotropic filtering level the textures that have mips are sampled with ("render.anisotropy", 1 to 16),
    /// before the cap of the GPU. Read when a texture (OpenGL) or a sampler (Vulkan) is made : a texture that already
    /// exists keeps the level it was made with until it is loaded again.
    float RequestedAnisotropy();
}

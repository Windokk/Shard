#include "texture_filtering.hpp"

#include "engine/core/config/cvar.hpp"

namespace Shard::Engine::Rendering {

    namespace {
        Core::CVar<int> cvAnisotropy("render.anisotropy", 16, "Anisotropic filtering of the textures that have mips (1 : off, up to 16). Applies to the textures loaded afterwards",
                                     Core::CVarFlags::Archive, 1, 16);
    }

    float RequestedAnisotropy()
    {
        return static_cast<float>(cvAnisotropy.Get());
    }
}

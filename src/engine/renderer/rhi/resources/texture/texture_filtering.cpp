#include "texture_filtering.hpp"

#include <algorithm>

#include "engine/assets/project/quality.hpp"
#include "engine/core/config/cvar.hpp"

namespace Shard::Engine::Rendering {

    namespace {
        Core::CVar<int> cvAnisotropy("render.anisotropy", 16, "Anisotropic filtering of the textures that have mips (1 : off, up to 16). Applies to the textures loaded afterwards",
                                     Core::CVarFlags::Archive, 1, 16);
    }

    float RequestedAnisotropy()
    {
        // The quality preset caps what the variable asks for
        return static_cast<float>(std::min<int>(cvAnisotropy.Get(), int(Projects::QualityMaxAnisotropy())));
    }
}
